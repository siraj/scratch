/*
    This file is part of Kompex SQLite Wrapper.
	Copyright (c) 2008-2012 Sven Broeske

    Kompex SQLite Wrapper is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Kompex SQLite Wrapper is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with Kompex SQLite Wrapper. If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>

#include "KompexSQLiteBlob.h"
#include "KompexSQLiteStatement.h"
#include "KompexSQLiteDatabase.h"
#include "KompexSQLiteException.h"

namespace Kompex
{

SQLiteBlob::SQLiteBlob():
	mBlobHandle(0)
{
}

SQLiteBlob::SQLiteBlob(SQLiteDatabase *db, std::string symbolicDatabaseName, std::string tableName, std::string columnName, int64 rowId, BLOB_ACCESS_MODE accessMode):
	mBlobHandle(0)
{
	OpenBlob(db, symbolicDatabaseName, tableName, columnName, rowId, accessMode);
}

SQLiteBlob::~SQLiteBlob()
{
	if(mBlobHandle != 0)
		CloseBlob();
}

void SQLiteBlob::OpenBlob(SQLiteDatabase *db, std::string symbolicDatabaseName, std::string tableName, std::string columnName, int64 rowId, BLOB_ACCESS_MODE accessMode)
{
     std::cout << "Trying to open blob\n";
	if(mBlobHandle != 0)
		CloseBlob();

    std::cout << "Didnt try to close blob\n";

	mDatabase = db;
	if(sqlite3_blob_open(mDatabase->GetDatabaseHandle(), symbolicDatabaseName.c_str(), tableName.c_str(), columnName.c_str(), rowId, accessMode, &mBlobHandle) != SQLITE_OK)
    {
        std::cout << sqlite3_errmsg(mDatabase->GetDatabaseHandle()) << "\n";
        KOMPEX_EXCEPT(sqlite3_errmsg(mDatabase->GetDatabaseHandle()));
    }
}

void SQLiteBlob::CloseBlob()
{
    std::cout << "Trying to close blob";
	if(sqlite3_blob_close(mBlobHandle) != SQLITE_OK)
    {
        std::cout << sqlite3_errmsg(mDatabase->GetDatabaseHandle()) << "\n";
		KOMPEX_EXCEPT(sqlite3_errmsg(mDatabase->GetDatabaseHandle()));
    }

	mBlobHandle = 0;
}

int SQLiteBlob::GetBlobSize() const
{
	if(mBlobHandle == 0)
		KOMPEX_EXCEPT("GetBlobSize() no open BLOB handle");

	return sqlite3_blob_bytes(mBlobHandle);
}

void SQLiteBlob::ReadBlob(void *buffer, int numberOfBytes, int offset)
{
	if(mBlobHandle == 0)
		KOMPEX_EXCEPT("ReadBlob() no open BLOB handle");
	if((offset + numberOfBytes) > GetBlobSize())
		KOMPEX_EXCEPT("ReadBlob() offset and numberOfBytes exceed the BLOB size");
		
	switch(sqlite3_blob_read(mBlobHandle, buffer, numberOfBytes, offset))
	{
		case SQLITE_OK:
			break;
		case SQLITE_ABORT:
			KOMPEX_EXCEPT("ReadBlob() BLOB handle expired - can not read BLOB");
		default:
			KOMPEX_EXCEPT(sqlite3_errmsg(mDatabase->GetDatabaseHandle()));
	}
}

void SQLiteBlob::WriteBlob(const void *buffer, int numberOfBytes, int offset)
{
	if(mBlobHandle == 0)
		KOMPEX_EXCEPT("WriteBlob() no open BLOB handle");
	if((offset + numberOfBytes) > GetBlobSize())
		KOMPEX_EXCEPT("WriteBlob() offset and numberOfBytes exceed the BLOB size");

	switch(sqlite3_blob_write(mBlobHandle, buffer, numberOfBytes, offset))
	{
		case SQLITE_OK:
			break;
		case SQLITE_ABORT:
			KOMPEX_EXCEPT("WriteBlob() BLOB handle expired - can not write BLOB");
		default:
			KOMPEX_EXCEPT(sqlite3_errmsg(mDatabase->GetDatabaseHandle()));
	}
}

}	// namespace Kompex
