/*
   Copyright (C) 2014 Preet Desai (preet.desai@gmail.com)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef SCRATCH_TILESET_LL_H
#define SCRATCH_TILESET_LL_H

#include <unordered_map>
#include <MiscUtils.h>
#include <TileDataSourceLL.h>
#include <TileVisibilityLL.h>

#include <LookupList.h>

namespace scratch
{
    class TileSetLL
    {
    public:
        struct TileItem
        {
            TileItem(TileLL::Id id,
                     TileLL const * tile,
                     TileLL const * sample,
                     TileDataSourceLL::Data const * data) :
                id(id),tile(tile),sample(sample),data(data)
            {
                // empty
            }

            // If sample is null, this TileItem does not
            // use sampling

            TileLL::Id id;
            TileLL const * tile;
            TileLL const * sample;
            TileDataSourceLL::Data const * data;
        };

        struct Options
        {
            // sane defaults
            Options() :
                max_level(18),
                max_tile_data(std::numeric_limits<uint64_t>::max()/2),
                cache_size_hint(128),
                list_preload_levels({0,1}),
                upsample_hint(false)
            {
                // empty
            }

            // The max level of tile subdivisions
            uint8_t max_level;

            // The max number of data associated to a tile
            // (textures, geometry, etc) from TileDataSource
            // thats allowed.
            uint64_t max_tile_data;

            // Hint for amount of TileDataSource::Data that is cached.
            // The amount may be exceeded if the number of visible tiles
            // during a given update is greater than the size hint.
            uint64_t cache_size_hint;

            // List of levels to preload tile data from.
            // max_tile_data includes preloaded data.
            std::vector<uint8_t> list_preload_levels;

            // Upsample data for individual TileItems from parent
            // tiles if its own data isn't available yet. The
            // TileDataSource implementation must allow sampling.
            bool upsample_hint;
        };

        TileSetLL(std::unique_ptr<TileDataSourceLL> tile_data_source,
                  std::unique_ptr<TileVisibilityLL> tile_visibility,
                  Options options);

        ~TileSetLL();

        GeoBounds const & GetBounds() const;

        uint8_t GetMaxLevel() const;

        uint8_t GetNumRootTilesX() const;

        uint8_t GetNumRootTilesY() const;

        void UpdateTileSet(osg::Camera const * cam,
                           std::vector<TileLL::Id> &list_tiles_add,
                           std::vector<TileLL::Id> &list_tiles_upd,
                           std::vector<TileLL::Id> &list_tiles_rem);

        void quickTest();

        TileItem const * GetTile(TileLL::Id tile_id) const;


        // TileItem Comparators
        // TODO why are these public
        static bool CompareTileItemPtrIdIncreasing(TileItem const * a,
                                                   TileItem const * b)
        {
            return (a->id < b->id);
        }

        static bool CompareTileItemIdIncreasing(TileItem const & a,
                                                TileItem const & b)
        {
            return (a.id < b.id);
        }

        static void GenerateSampleTexCoords(TileLL const * from,
                                            TileLL const * to,
                                            double &s_start,
                                            double &s_delta,
                                            double &t_start,
                                            double &t_delta)
        {
            double const from_width = from->bounds.maxLon-from->bounds.minLon;
            double const from_height= from->bounds.maxLat-from->bounds.minLat;

            s_start = (to->bounds.minLon-from->bounds.minLon)/from_width;
            s_delta = (to->bounds.maxLon-to->bounds.minLon)/from_width;

            t_start = (to->bounds.minLat-from->bounds.minLat)/from_height;
            t_delta = (to->bounds.maxLat-to->bounds.minLat)/from_height;
        }

        static void GenerateSampleTexCoords(TileLL const * from,
                                            TileLL const * to,
                                            osg::Vec4 &region)
        {
            double const from_width = from->bounds.maxLon-from->bounds.minLon;
            double const from_height= from->bounds.maxLat-from->bounds.minLat;

            // s_start
            region.x() = (to->bounds.minLon-from->bounds.minLon)/from_width;

            // s_delta
            region.z() = (to->bounds.maxLon-to->bounds.minLon)/from_width;

            // t_start
            region.y() = (to->bounds.minLat-from->bounds.minLat)/from_height;

            // t_delta
            region.w() = (to->bounds.maxLat-to->bounds.minLat)/from_height;
        }

    private:
        struct TileMetaData : public TileLL::Data
        {
            TileMetaData(TileLL * tile) :
                tile(tile),
                request(nullptr),
                ready(false),
                is_visible(false),
                norm_error(-1.0)
            {
                // empty
            }

            ~TileMetaData() {}

            TileLL * tile;
            TileDataSourceLL::Request const * request;

            bool ready;
            bool is_visible;
            double norm_error;
            osg::Vec3d closest_point;
        };

        // TODO desc
        std::vector<TileItem> buildTileSetBFS();
        std::vector<TileItem> buildTileSetBFS_czm();

        std::vector<TileItem> buildTileSetRanked();


        static bool compareMetaDataRankIncreasing(TileMetaData const * a,
                                                  TileMetaData const * b);

        TileDataSourceLL::Data const *
        getData(TileLL const *tile);

        TileDataSourceLL::Request const *
        getDataRequest(TileLL const * tile,
                       bool reuse=false);

        TileDataSourceLL::Request const *
        getOrCreateDataRequest(TileLL const * tile,
                               bool reuse=false,
                               bool * existed=nullptr);

        std::vector<TileMetaData*>
        getOrCreateChildData(TileLL * tile,
                             bool & child_data_ready);

        void createChildren(TileLL * tile) const; // TODO inline

        void destroyChildren(TileLL * tile) const; // TODO inline

        // * create and attach TileMetaData for all
        //   children of @tile and return references
        // * returned list is organized by rough
        //   distance from @lla
        // TODO rename 'createChildMetaData'...?
        std::vector<TileMetaData*>
        createChildrenMetaData(TileLL const * tile,
                               LLA const &lla) const;

        TileMetaData * createMetaData(TileLL * tile) const
        {
            tile->data.reset(new TileMetaData(tile));
            return static_cast<TileMetaData*>(tile->data.get());
        }

        TileMetaData * getMetaData(TileLL * tile) const
        {
            return static_cast<TileMetaData*>(tile->data.get());
        }


        // init helpers
        Options initOptions(Options opts) const;
        uint64_t initNumPreloadData() const;
        uint64_t initMaxViewData() const;

        //
        std::unique_ptr<TileDataSourceLL> m_tile_data_source;
        std::unique_ptr<TileVisibilityLL> m_tile_visibility;
        Options const m_opts;
        uint64_t const m_num_preload_data;
        uint64_t const m_max_view_data;

        //
        std::vector<std::unique_ptr<TileLL>> m_list_root_tiles;

        // 0 = view, 1 = preload
        std::vector<uint8_t> m_list_level_is_preloaded;

        // lkup_preloaded_data
        std::map<
            TileLL::Id,
            std::shared_ptr<TileDataSourceLL::Request>
            > m_lkup_preloaded_data;

        // lru_view_data
        LookupList<
            TileLL::Id,
            std::shared_ptr<TileDataSourceLL::Request>,
            std::map
            > m_ll_view_data;

        bool m_preloaded_data_ready;

        // camera eye LLA
        LLA m_lla_cam_eye;

        std::vector<TileItem> m_list_tiles;
        std::vector<TileItem> m_list_tiles_prev;
        std::vector<TileItem> m_list_tiles_next;
    };
}

#endif // SCRATCH_TILESET_LL_H
