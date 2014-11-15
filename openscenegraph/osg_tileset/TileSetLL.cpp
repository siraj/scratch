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

#include <TileSetLL.h>

namespace scratch
{
    TileSetLL::TileSetLL(std::unique_ptr<TileDataSourceLL> tile_data_source,
                         std::unique_ptr<TileVisibilityLL> tile_visibility,
                         Options options) :
        m_tile_data_source(std::move(tile_data_source)),
        m_tile_visibility(std::move(tile_visibility)),
        m_opts(initOptions(options)),
        m_num_preload_data(initNumPreloadData()),
        m_max_view_data(initMaxViewData())
    {
        // debug
        std::cout << "m_opts.max_data: " << m_opts.max_data << std::endl;
        std::cout << "m_num_preload_data: " << m_num_preload_data << std::endl;
        std::cout << "m_max_view_data: " << m_max_view_data << std::endl;

        // Generate root tiles from the number of
        // root tiles in x and y
        auto const bounds = this->GetBounds();
        auto const num_root_tiles_x = this->GetNumRootTilesX();
        auto const num_root_tiles_y = this->GetNumRootTilesY();

        double const lon_width =
                (bounds.maxLon-bounds.minLon)/num_root_tiles_x;

        double const lat_width =
                (bounds.maxLat-bounds.minLat)/num_root_tiles_y;

        for(size_t y=0; y < num_root_tiles_y; y++) {
            for(size_t x=0; x < num_root_tiles_x; x++) {
                // Create bounds for this tile
                GeoBounds b(bounds.minLon,
                            bounds.minLon+(lon_width*(x+1)),
                            bounds.minLat,
                            bounds.minLat+(lat_width*(y+1)));

                // save
                m_list_root_tiles.emplace_back(new TileLL(b,x,y));
            }
        }


        // Preload the base textures
        m_preloaded_data_ready = false;

        m_list_level_is_preloaded.resize(m_opts.max_level,0);

        for(auto level : m_opts.list_preload_levels) {
            // mark as a preload level
            m_list_level_is_preloaded[level] = 1;

            // request data for all tiles in this level
            auto const tiles_in_x = ipow(2,level)*num_root_tiles_x;
            auto const tiles_in_y = ipow(2,level)*num_root_tiles_y;

            for(int64_t y=0; y < tiles_in_y; y++) {
                for(int64_t x=0; x < tiles_in_x; x++) {
                    // save request
                    auto const tile_id =
                            TileLL::GetIdFromLevelXY(level,x,y);

                    m_lkup_preloaded_data.emplace(
                                tile_id,
                                m_tile_data_source->RequestData(
                                    tile_id));
                }
            }
        }       
    }

    TileSetLL::~TileSetLL()
    {
        // empty
    }

    GeoBounds const & TileSetLL::GetBounds() const
    {
        return m_tile_data_source->GetBounds();
    }

    uint8_t TileSetLL::GetMinLevel() const
    {
        return m_opts.min_level;
    }

    uint8_t TileSetLL::GetMaxLevel() const
    {
        return m_opts.max_level;
    }

    uint8_t TileSetLL::GetNumRootTilesX() const
    {
        return m_tile_data_source->GetNumRootTilesX();
    }

    uint8_t TileSetLL::GetNumRootTilesY() const
    {
        return m_tile_data_source->GetNumRootTilesY();
    }

    void TileSetLL::UpdateTileSet(osg::Camera const * cam,
                                  std::vector<TileLL::Id> &list_tile_id_add,
                                  std::vector<TileLL::Id> &list_tile_id_upd,
                                  std::vector<TileLL::Id> &list_tile_id_rem)
    {
        // Ensure the base data has been loaded
        if(!m_preloaded_data_ready) {
            for(auto const &id_req : m_lkup_preloaded_data) {
                if(!id_req.second->IsFinished()) {
                    // We don't do anything until all of
                    // the base data is finished loading
                    return;
                }
            }
            m_preloaded_data_ready = true;
            std::cout << "#: [loaded base data]" << std::endl;
        }

        // Update tile visibility
        m_tile_visibility->Update(cam);

        // Build tile set
        std::vector<TileItem> list_tiles_new = buildTileSetBFS();

        // We need to sort by tile_id before we can
        // split into tiles added/removed
        std::sort(list_tiles_new.begin(),
                  list_tiles_new.end(),
                  TileSetLL::CompareTileItemIdIncreasing);

        // Create new and old id lists
        std::vector<TileLL::Id> list_tile_id_new;
        list_tile_id_new.reserve(list_tiles_new.size());
        for(auto const &item : list_tiles_new) {
            list_tile_id_new.push_back(item.id);
        }

        std::vector<TileLL::Id> list_tile_id_old;
        list_tile_id_old.reserve(m_list_tiles.size());
        for(auto const &item : m_list_tiles) {
            list_tile_id_old.push_back(item.id);
        }

        SplitSets(list_tile_id_new,
                  list_tile_id_old,
                  list_tile_id_add,
                  list_tile_id_rem,
                  list_tile_id_upd);

        // save new tile set
        std::swap(m_list_tiles,list_tiles_new);
    }

    TileSetLL::TileItem const * TileSetLL::GetTile(TileLL::Id tile_id) const
    {
        // expect m_list_tiles to be sorted
        // in increasing order (default)

        // TODO check if function or functor faster than lambda
        auto it = std::lower_bound(
                    m_list_tiles.begin(),
                    m_list_tiles.end(),
                    tile_id,
                    [](TileItem const &a, TileLL::Id b) {
                        return (a.id < b);
                    });

        if(it == m_list_tiles.end()) {
            return nullptr;
        }

        return (&(*it));
    }

    std::vector<TileSetLL::TileItem> TileSetLL::buildTileSetBFS()
    {
        // Method based on how CesiumJS creates tiles

        // Build the tileset by doing a breadth first search
        // on all of the root tiles
        std::vector<TileMetaData> queue_bfs;
        std::vector<TileItem> list_tile_items;

        // Enqueue all root tiles first, this ensures
        // a contiguous tileset
        for(auto & tile : m_list_root_tiles)
        {
            if(queue_bfs.size() == m_opts.max_data) {
                break;
            }

            bool is_visible,exceeds_err;
            m_tile_visibility->GetVisibility(
                        tile.get(),is_visible,exceeds_err);

//            if(!is_visible) {
//                continue;
//            }

            queue_bfs.emplace_back(
                        tile.get(),
                        is_visible,
                        exceeds_err,
                        getOrCreateDataRequest(tile.get()));
        }

        // Mark the start of this update/tile traversal in
        // the view data LRU cache.
        m_lru_view_data.mark_head();

        // Traverse each root tile in BFS order and save tiles
        // that are:
        // * ready
        // * are either leaves (wrt to the current visibility
        //   error) or have children that aren't ready

        // Parent tiles must be ready before children can be traversed

        for(size_t i=0; i < queue_bfs.size(); i++)
        {
            if(queue_bfs.size() == m_opts.max_data) {
                break;
            }

            TileMetaData & meta = queue_bfs[i];
            TileLL * tile = meta.tile;

            if(meta.request->IsFinished()) {
                // This tile's data is ready
                bool save_this_tile = true;

                if(meta.exceeds_err &&
                  (tile->level < m_opts.max_level) &&
                  (queue_bfs.size()+4 <= m_opts.max_data)) {
                    // This tile exceeds the error metric for
                    // subdivision

                    // Check if this tile's children are ready
                    bool child_data_ready;
                    std::vector<TileMetaData> const list_children =
                            getOrCreateChildDataRequests(
                                tile,child_data_ready);

                    if(child_data_ready) {
                        // children are ready, so enqueue them to be
                        // saved and do not save the parent
                        for(auto & meta_child : list_children) {
                            queue_bfs.push_back(meta_child);
                        }
                        save_this_tile = false;
                    }
                }

                if(save_this_tile) {
                    TileItem item;
                    item.id = meta.tile->id;
                    item.tile = meta.tile;
                    item.data = meta.request->GetData().get();

                    list_tile_items.push_back(item);

                    // destroy children if they exist
                    destroyChildren(tile);
                }
            }
        }

//        std::cout << "queue_bfs: " << std::endl;
//        for(auto & tmd : queue_bfs) {
//            std::cout << int(tmd.tile->level)
//                      << "," << tmd.tile->x
//                      << "," << tmd.tile->y
//                      << std::endl;
//        }

        // Trim the tile data cache according to
        // m_opts.cache_size_hint. Only data inserted before
        // the requests in this BFS traversal (before mark_head()
        // was called) can be trimmed, even if the total size of
        // the cache exceeds the size hint.

        // This is irrelevant if m_opts.max_data is less
        // than m_opts.cache_size_hint, as that will actively
        // limit the cache size as entries are inserted

        m_lru_view_data.trim_against_mark(m_opts.cache_size_hint);

        std::cout << "#: cache sz: "
                  << m_lru_view_data.size()
                  << std::endl;

        return list_tile_items;
    }

//    std::vector<TileSetLL::TileMetaData>
//    TileSetLL::getOrCreateChildDataRequests(TileLL * tile,
//                                            bool & child_data_ready)
//    {
//        // create children first if required
//        createChildren(tile);

//        std::vector<TileMetaData> list_children {
//            TileMetaData(tile->tile_LT.get()),
//            TileMetaData(tile->tile_LB.get()),
//            TileMetaData(tile->tile_RB.get()),
//            TileMetaData(tile->tile_RT.get())
//        };

//        for(auto it = list_children.begin();
//            it != list_children.end();)
//        {
//            TileMetaData & meta_child = (*it);

//            m_tile_visibility->GetVisibility(
//                        meta_child.tile,
//                        meta_child.is_visible,
//                        meta_child.exceeds_err);

//            if(meta_child.is_visible) {
//                ++it;
//            }
//            else {
//                it = list_children.erase(it);
//            }
//        }

//        // Check if the data for each child tile is ready
//        child_data_ready = true;
//        for(auto & meta_child : list_children) {
//            // create the request if it doesnt already exist
//            meta_child.request =
//                    getOrCreateDataRequest(meta_child.tile);

//            if(!meta_child.request->IsFinished()) {
//                child_data_ready = child_data_ready && false;
//            }
//        }

//        return list_children;
//    }

    std::vector<TileSetLL::TileMetaData>
    TileSetLL::getOrCreateChildDataRequests(TileLL * tile,
                                            bool & child_data_ready)
    {
        // create children first if required
        createChildren(tile);

        std::vector<TileMetaData> list_children {
            TileMetaData(tile->tile_LT.get()),
            TileMetaData(tile->tile_LB.get()),
            TileMetaData(tile->tile_RB.get()),
            TileMetaData(tile->tile_RT.get())
        };

        // Check if the data for each child tile is ready
        child_data_ready = true;
        for(auto & meta_child : list_children) {
            // create the request if it doesnt already exist
            meta_child.request =
                    getOrCreateDataRequest(meta_child.tile);

            if(!meta_child.request->IsFinished()) {
                child_data_ready = child_data_ready && false;
            }
        }

        // If the child data is ready, calculate its
        // visibility as well
        if(child_data_ready) {
            for(auto & meta_child : list_children) {
                m_tile_visibility->GetVisibility(
                            meta_child.tile,
                            meta_child.is_visible,
                            meta_child.exceeds_err);
            }
        }

        return list_children;
    }

    TileDataSourceLL::Request const *
    TileSetLL::getOrCreateDataRequest(TileLL const * tile)
    {
        // Check if this tile data has been preloaded
        if(m_list_level_is_preloaded[tile->level]) {
            auto it = m_lkup_preloaded_data.find(tile->id);
            return it->second.get();
        }

        // Create the request if it doesn't exist in cache
        bool exists;
        TileDataSourceLL::Request * data_req =
                m_lru_view_data.get(tile->id,true,exists).get();

        if(!exists) {
            m_lru_view_data.insert(
                        tile->id,
                        m_tile_data_source->RequestData(tile->id),
                        false);

            data_req = m_lru_view_data.get(
                        tile->id,false,exists).get();
        }

        return data_req;
    }

    void TileSetLL::createChildren(TileLL *tile) const
    {
        if(tile->clip == TileLL::k_clip_NONE) {
            uint32_t const x = tile->x*2;
            uint32_t const y = tile->y*2;
            tile->tile_LT.reset(new TileLL(tile,x,y+1));
            tile->tile_LB.reset(new TileLL(tile,x,y));
            tile->tile_RB.reset(new TileLL(tile,x+1,y));
            tile->tile_RT.reset(new TileLL(tile,x+1,y+1));
            tile->clip = TileLL::k_clip_ALL;
        }
    }

    void TileSetLL::destroyChildren(TileLL *tile) const
    {
        if(tile->clip == TileLL::k_clip_ALL) {
            tile->tile_LT = nullptr;
            tile->tile_LB = nullptr;
            tile->tile_RB = nullptr;
            tile->tile_RT = nullptr;
            tile->clip = TileLL::k_clip_NONE;
        }
    }

    TileSetLL::Options
    TileSetLL::initOptions(Options opts) const
    {
        // max_data must be less than the max possible
        // value of its data type to allow for safe
        // comparisons (ie, if(x > max_data))
        assert(opts.max_data < std::numeric_limits<uint64_t>::max());

        // Ensure min and max levels are between
        // corresponding source levels
        if(opts.min_level > opts.max_level) {
            std::swap(opts.min_level,opts.max_level);
        }
        else if(opts.min_level == opts.max_level) {
            opts.max_level++;
        }

        opts.min_level = clamp(opts.min_level,
                               m_tile_data_source->GetMinLevel(),
                               m_tile_data_source->GetMaxLevel());

        opts.max_level = clamp(opts.max_level,
                               m_tile_data_source->GetMinLevel(),
                               m_tile_data_source->GetMaxLevel());

        // The preload level list must be sorted
        // in increasing order
        std::sort(opts.list_preload_levels.begin(),
                  opts.list_preload_levels.end());

        // max_data must be equal to or larger than
        // the total number of preload tiles if a valid
        // limit is set
        uint64_t num_base_data = 0;
        for(auto level : opts.list_preload_levels) {
            num_base_data += ipow(4,level);
        }

        if(num_base_data > opts.max_data) {
            opts.max_data = num_base_data;
        }

        // TODO check upsampling hint

        return opts;
    }

    uint64_t TileSetLL::initNumPreloadData() const
    {
        uint64_t num_preload_data=0;
        for(auto level : m_opts.list_preload_levels) {
            num_preload_data += ipow(4,level);
        }

        return num_preload_data;
    }

    uint64_t TileSetLL::initMaxViewData() const
    {
        uint64_t num_view_data =
                m_opts.max_data-
                m_num_preload_data;

        return num_view_data;
    }

} // scratch
