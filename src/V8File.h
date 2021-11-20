/*----------------------------------------------------------
This Source Code Form is subject to the terms of the
Mozilla Public License, v.2.0. If a copy of the MPL
was not distributed with this file, You can obtain one
at http://mozilla.org/MPL/2.0/.
----------------------------------------------------------*/
/////////////////////////////////////////////////////////////////////////////
//
//
//	Author:			disa_da
//	E-mail:			disa_da2@mail.ru
//
//
/////////////////////////////////////////////////////////////////////////////

/**
    2014-2021       dmpas           sergey(dot)batanov(at)dmpas(dot)ru
    2019-2020       fishca          fishcaroot(at)gmail(dot)com
 */

// V8File.h: interface for the CV8File class.
//
//////////////////////////////////////////////////////////////////////

/*#if !defined(AFX_V8FILE_H__935D5C2B_70FA_45F2_BDF2_A0274A8FD60C__INCLUDED_)
#define AFX_V8FILE_H__935D5C2B_70FA_45F2_BDF2_A0274A8FD60C__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000*/

#define _CRT_SECURE_NO_WARNINGS
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))

#include <cassert>
#include <string>
#include "zlib.h"
#include <fstream>

#include <cstdint>
#include <vector>
#include <boost/shared_array.hpp>
#include <boost/filesystem.hpp>
#include <algorithm>

namespace v8unpack {

typedef uint32_t UINT;
typedef uint32_t DWORD;
typedef uint32_t ULONG;
typedef uint64_t ULONGLONG;

const size_t V8_DEFAULT_PAGE_SIZE = 512;
const uint32_t V8_FF_SIGNATURE = 0x7fffffff;

// Для конфигурации старше 8.3.16, без режима совместимости
const uint64_t V8_FF64_SIGNATURE = 0xffffffffffffffff;
const size_t V8_OFFSET_8316 = 0x1359;  // волшебное смещение, откуда такая цифра неизвестно...


#define CHUNK 16384
#ifndef DEF_MEM_LEVEL
#  if MAX_MEM_LEVEL >= 8
#    define DEF_MEM_LEVEL 8
#  else
#    define DEF_MEM_LEVEL  MAX_MEM_LEVEL
#  endif
#endif


#define V8UNPACK_ERROR (-50)
#define V8UNPACK_NOT_V8_FILE (V8UNPACK_ERROR-1)
#define V8UNPACK_HEADER_ELEM_NOT_CORRECT (V8UNPACK_ERROR-2)


#define V8UNPACK_INFLATE_ERROR (V8UNPACK_ERROR-20)
#define V8UNPACK_INFLATE_IN_FILE_NOT_FOUND (V8UNPACK_INFLATE_ERROR-1)
#define V8UNPACK_INFLATE_OUT_FILE_NOT_CREATED (V8UNPACK_INFLATE_ERROR-2)
#define V8UNPACK_INFLATE_DATAERROR (V8UNPACK_INFLATE_ERROR-3)

#define V8UNPACK_DEFLATE_ERROR (V8UNPACK_ERROR-30)
#define V8UNPACK_DEFLATE_IN_FILE_NOT_FOUND (V8UNPACK_ERROR-1)
#define V8UNPACK_DEFLATE_OUT_FILE_NOT_CREATED (V8UNPACK_ERROR-2)

#define SHOW_USAGE (-22)


DWORD _httoi(const char *value);
ULONGLONG _httoi64(const char *value);

class CV8Elem;

class CV8File
{
public:

	struct stFileHeader
	{
		DWORD next_page_addr;
		DWORD page_size;
		DWORD storage_ver;
		DWORD reserved; // всегда 0x00000000 ?

		static UINT Size()
		{
			return 4 + 4 + 4 + 4;
		}

	};

	struct stFileHeader64
	{
		ULONGLONG next_page_addr;  // 64 бита стало
		DWORD page_size;
		DWORD storage_ver;
		DWORD reserved; // всегда 0x00000000 ?

		static UINT Size()
		{
			return 8 + 4 + 4 + 4;
		}
	};

	struct stElemAddr
	{
		DWORD elem_header_addr;
		DWORD elem_data_addr;
		DWORD fffffff; //всегда 0x7fffffff ?

		static UINT Size()
		{
			return 4 + 4 + 4;
		}

		static const uint32_t UNDEFINED_VALUE = 0x7fffffff;
	};

	struct stElemAddr64
	{
		// каждый элемент стал 64 бита
		ULONGLONG elem_header_addr;
		ULONGLONG elem_data_addr;
		ULONGLONG fffffff; //всегда 0xffffffffffffffff ?

		static UINT Size()
		{
			return 8 + 8 + 8;
		}

		static const uint64_t UNDEFINED_VALUE = 0xffffffffffffffff;
	};

	struct stBlockHeader
	{
		char EOL_0D;
		char EOL_0A;
		char data_size_hex[8] = {'0', '0', '0', '0', '0', '0', '0', '0'};
		char space1;
		char page_size_hex[8] = {'0', '0', '0', '0', '0', '0', '0', '0'};
		char space2;
		char next_page_addr_hex[8] = {'7', 'f', 'f', 'f', 'f', 'f', 'f', 'f'};
		char space3;
		char EOL2_0D;
		char EOL2_0A;

		stBlockHeader():
			EOL_0D(0xd), EOL_0A(0xa),
			space1(' '), space2(' '), space3(' '),
			EOL2_0D(0xd), EOL2_0A(0xa)
			{}

		static stBlockHeader create(uint32_t block_data_size, uint32_t page_size, uint32_t next_page_addr);

		static UINT Size()
		{
			return 1 + 1 + 8 + 1 + 8 + 1 + 8 + 1 + 1 + 1;
		};

		bool IsCorrect() const
		{
			return EOL_0D == 0x0d
				&& EOL_0A == 0x0a
				&& space1 == 0x20
				&& space2 == 0x20
				&& space3 == 0x20
				&& EOL2_0D == 0x0d
				&& EOL2_0A == 0x0a;
		}

		uint32_t data_size() const {
			return _httoi(data_size_hex);
		}

		uint32_t page_size() const {
			return _httoi(page_size_hex);
		}

		uint32_t next_page_addr() const {
			return _httoi(next_page_addr_hex);
		}

		static const uint32_t UNDEFINED_VALUE = 0x7fffffff;
	};

	struct stBlockHeader64
	{
		char EOL_0D;
		char EOL_0A;
		char data_size_hex[16] = { ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ' }; // 64 бита теперь
		char space1;
		char page_size_hex[16] = { ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ' }; // 64 бита теперь
		char space2;
		char next_page_addr_hex[16] = { ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ' }; // 64 бита теперь
		char space3;
		char EOL2_0D;
		char EOL2_0A;

		stBlockHeader64() :
			EOL_0D(0xd), EOL_0A(0xa),
			space1(' '), space2(' '), space3(' '),
			EOL2_0D(0xd), EOL2_0A(0xa)
		{

		}

		static stBlockHeader64 create(ULONGLONG block_data_size, ULONGLONG page_size, ULONGLONG next_page_addr);

		static UINT Size()
		{
			return 1 + 1 + 16 + 1 + 16 + 1 + 16 + 1 + 1 + 1; // 55 теперь
		};

		bool IsCorrect() const
		{
			return EOL_0D == 0x0d
				&& EOL_0A == 0x0a
				&& space1 == 0x20
				&& space2 == 0x20
				&& space3 == 0x20
				&& EOL2_0D == 0x0d
				&& EOL2_0A == 0x0a;
		}

		uint64_t data_size() const {
			return _httoi64(data_size_hex);
		}

		uint64_t page_size() const {
			return _httoi64(page_size_hex);
		}

		uint64_t next_page_addr() const {
			return _httoi64(next_page_addr_hex);
		}

		static const uint64_t UNDEFINED_VALUE = 0xffffffffffffffff;
	};

	struct Format15
	{
		typedef stFileHeader  file_header_t;
		typedef stBlockHeader block_header_t;
		typedef stElemAddr    elem_addr_t;

		static const uint32_t UNDEFINED_VALUE = 0x7fffffff;
		static const std::streamoff BASE_OFFSET = 0;
	};

	struct Format16
	{
		typedef stFileHeader64  file_header_t;
		typedef stBlockHeader64 block_header_t;
		typedef stElemAddr64    elem_addr_t;

		static const uint64_t UNDEFINED_VALUE = 0xffffffffffffffff;
		static const std::streamoff BASE_OFFSET = 0x1359;
	};

	int GetData(char **DataBufer, ULONG *DataBuferSize);
	int Pack();
	int LoadFileFromFolder(const std::string &dirname);
	int LoadFile(char *pFileData, ULONG FileData, bool boolInflate = true, bool UnpackWhenNeed = false);
	int SaveFileToFolder(const boost::filesystem::path &directiory) const;

	CV8File();
	explicit CV8File(char *pFileData, bool boolUndeflate = true);
	virtual ~CV8File();

	CV8File(const CV8File &src);

	void Dispose();

	static int PackFromFolder(const std::string &dirname, const std::string &filename);
	static int BuildCfFile(const std::string &dirname, const std::string &filename, bool dont_deflate = false);
	static int SaveBlockData(std::basic_ostream<char> &file_out, const char *pBlockData, UINT BlockDataSize, UINT PageSize = 512);
	static int SaveBlockData(std::basic_ostream<char> &file_out, std::basic_istream<char> &file_in, UINT BlockDataSize, UINT PageSize = 512);
	static int UnpackToFolder(const std::string &filename, const std::string &dirname, const std::string &block_name, bool print_progress = false);

	static int UnpackToDirectoryNoLoad(
		const std::string                &directory,
		      std::basic_istream<char>   &file,
		const std::vector<std::string>   &filter,
		      bool                        boolInflate = true,
		      bool                        UnpackWhenNeed = false
	);

	static int UnpackToDirectoryNoLoad16(
		const std::string                &directory,
		      std::basic_istream<char>   &file,
		const std::vector<std::string>   &filter,
		      bool                        boolInflate = true,
		      bool                        UnpackWhenNeed = false
	);

	static int Parse(
		const std::string                &filename,
		const std::string                &dirname,
		const std::vector< std::string > &filter
	);

	static int ListFiles(const std::string &filename);
	static bool IsV8File(const char *pFileData, ULONG FileDataSize);
	static bool IsV8File16(const char *pFileData, ULONG FileDataSize);
	static bool IsV8File(std::basic_istream<char> &file);
	static bool IsV8File16(std::basic_istream<char>& file);

private:
	stFileHeader                FileHeader;
	std::vector<stElemAddr>     ElemsAddrs;

	std::vector<CV8Elem>        Elems;
	bool                        IsDataPacked;
};

class CV8Elem
{
public:

	struct stElemHeaderBegin
	{
		ULONGLONG date_creation;
		ULONGLONG date_modification;
		DWORD res; // всегда 0x000000?
		//изменяемая длина имени блока
		//после имени DWORD res; // всегда 0x000000?
		static UINT Size()
		{
			return 8 + 8 + 4;
		};
	};

	CV8Elem(const CV8Elem &src);
	CV8Elem();
	~CV8Elem();

	int           Pack(bool deflate = true);
	int           SetName(const std::string &ElemName);
	std::string   GetName() const;

	void Dispose();

	char               *pHeader;
	UINT                HeaderSize;
	char               *pData;
	UINT                DataSize;
	CV8File             UnpackedData;
	bool                IsV8File;
	bool                NeedUnpack;

};

int Deflate(std::istream &source, std::ostream &dest);
int Inflate(std::istream &source, std::ostream &dest);

int Deflate(const std::string &in_filename, const std::string &out_filename);
int Inflate(const std::string &in_filename, const std::string &out_filename);

int Deflate(const char* in_buf, char** out_buf, ULONG in_len, ULONG* out_len);
int Inflate(const char* in_buf, char** out_buf, ULONG in_len, ULONG* out_len);

template<typename T>
void full_copy(std::basic_istream<T> &in_file, std::basic_ostream<T> &out_file)
{
	std::copy(
			std::istreambuf_iterator<T>(in_file),
			std::istreambuf_iterator<T>(),
			std::ostreambuf_iterator<T>(out_file)
	);
}

} // namespace v8unpack