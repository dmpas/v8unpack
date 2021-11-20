/*----------------------------------------------------------
This Source Code Form is subject to the terms of the
Mozilla Public License, v.2.0. If a copy of the MPL
was not distributed with this file, You can obtain one
at http://mozilla.org/MPL/2.0/.
----------------------------------------------------------*/
/////////////////////////////////////////////////////////////////////////////
//	Author:			disa_da
//	E-mail:			disa_da2@mail.ru
/////////////////////////////////////////////////////////////////////////////
/**
    2014-2021       dmpas       sergey(dot)batanov(at)dmpas(dot)ru
    2019-2020       fishca      fishcaroot(at)gmail(dot)com
 */

#include "V8File.h"
#include "zlib.h"
#include <iostream>
#include <iterator>

namespace v8unpack {

using namespace std;

CV8File::CV8File()
{
    IsDataPacked = true;
}


CV8File::CV8File(const CV8File &src)
    : FileHeader(src.FileHeader), IsDataPacked(src.IsDataPacked)
{
    ElemsAddrs.assign(src.ElemsAddrs.begin(), src.ElemsAddrs.end());
    Elems.assign(src.Elems.begin(), src.Elems.end());
}


CV8File::CV8File(char *pFileData, bool boolInflate)
{
    LoadFile(pFileData, boolInflate);
}

CV8File::~CV8File()
{
}

CV8Elem::CV8Elem()
	: pHeader(nullptr), HeaderSize(0),
	pData(nullptr), DataSize(0),
	IsV8File(false), NeedUnpack(false)
{
}

CV8Elem::CV8Elem(const string &name)
{
	HeaderSize = CV8Elem::stElemHeaderBegin::Size() + name.size() * 2 + 4; // последние четыре всегда нули?
	pHeader = new char[HeaderSize];

	memset(pHeader, 0, HeaderSize);

	SetName(name);
}


CV8Elem::~CV8Elem()
{
    // TODO: Добавить удаление данных
}


template<typename format>
static size_t
ReadBlockData(char *pFileData, typename format::block_header_t *pBlockHeader, char *&pBlockData)
{
	if (pBlockHeader == nullptr) {
		return 0;
	}

	size_t data_size;
	size_t read_in_bytes;

	data_size = pBlockHeader->data_size();
	pBlockData = new char[data_size];
	if (!pBlockData) {
		std::cerr << "ReadBlockData. BlockData == nullptr." << std::endl;
		return (size_t)(-1);
	}

	read_in_bytes = 0;
	while (read_in_bytes < data_size) {

		auto page_size = pBlockHeader->page_size();
		auto next_page_addr = pBlockHeader->next_page_addr();
		auto bytes_to_read = MIN(page_size, data_size - read_in_bytes);

		memcpy(&pBlockData[read_in_bytes], (char*)(&pBlockHeader[1]), bytes_to_read);

		read_in_bytes += bytes_to_read;

		if (next_page_addr != format::UNDEFINED_VALUE) // есть следующая страница
			pBlockHeader = (typename format::block_header_t*) &pFileData[next_page_addr + format::BASE_OFFSET];
		else
			break;
	}

	return data_size;
}

template<typename format>
static size_t
ReadBlockData(std::basic_istream<char> &file, typename format::block_header_t *pBlockHeader, char *&pBlockData)
{
	if (pBlockHeader == nullptr) {
		return 0;
	}

	uint64_t data_size;
	uint32_t read_in_bytes;

	typename format::block_header_t Header;
	data_size = pBlockHeader->data_size();
	pBlockData = new char[data_size];
	if (!pBlockData) {
		std::cerr << "ReadBlockData. BlockData == nullptr." << std::endl;
		return (size_t)(-1);
	}
	Header = *pBlockHeader;
	pBlockHeader = &Header;

	read_in_bytes = 0;
	while (read_in_bytes < data_size) {

		auto page_size = pBlockHeader->page_size();
		auto next_page_addr = pBlockHeader->next_page_addr();
		auto bytes_to_read = MIN(page_size, data_size - read_in_bytes);

		file.read(&pBlockData[read_in_bytes], bytes_to_read);

		read_in_bytes += bytes_to_read;

		if (next_page_addr != format::UNDEFINED_VALUE) { // есть следующая страница
			file.seekg(next_page_addr + format::BASE_OFFSET, std::ios_base::beg);
			file.read((char*)&Header, sizeof(Header));
		}
		else
			break;
	}

	return data_size;
}

template<typename format>
static int ReadBlockData(std::basic_istream<char> &file, const typename format::block_header_t &firstBlockHeader, std::basic_ostream<char> &out)
{
	uint32_t read_in_bytes;

	auto data_size = firstBlockHeader.data_size();
	auto Header = firstBlockHeader;
	auto pBlockHeader = &Header;

	read_in_bytes = 0;
	while (read_in_bytes < data_size) {

		auto page_size = pBlockHeader->page_size();
		auto next_page_addr = pBlockHeader->next_page_addr();

		auto bytes_to_read = MIN(page_size, data_size - read_in_bytes);

		const int buf_size = 1024; // TODO: Настраиваемый размер буфера
		char *pBlockData = new char[buf_size];
		uint32_t read_done = 0;

		while (read_done < bytes_to_read) {
			file.read(pBlockData, MIN(buf_size, bytes_to_read - read_done));
			uint32_t rd = file.gcount();
			out.write(pBlockData, rd);
			read_done += rd;
		}

		delete[] pBlockData;

		read_in_bytes += bytes_to_read;

		if (next_page_addr != format::UNDEFINED_VALUE) { // есть следующая страница
			file.seekg(next_page_addr + format::BASE_OFFSET, std::ios_base::beg);
			file.read((char*)&Header, sizeof(Header));
		}
		else
			break;
	}

	return 0;
}


template<typename format>
static int SaveBlockDataToBuffer(char **cur_pos, const char *pBlockData, uint32_t BlockDataSize, uint32_t PageSize = 0)
{
	if (PageSize < BlockDataSize)
		PageSize = BlockDataSize;

	typename format::block_header_t CurBlockHeader = format::block_header_t::create(BlockDataSize, PageSize, format::UNDEFINED_VALUE);
	memcpy(*cur_pos, (char*)&CurBlockHeader, format::block_header_t::Size());
	*cur_pos += format::block_header_t::Size();

	memcpy(*cur_pos, pBlockData, BlockDataSize);
	*cur_pos += BlockDataSize;

	for(uint32_t i = 0; i < PageSize - BlockDataSize; i++) {
		**cur_pos = 0;
		++*cur_pos;
	}

	return 0;
}

int CV8File::LoadFile(char *pFileData, uint32_t FileDataSize, bool boolInflate, bool UnpackWhenNeed)
{
    if (!pFileData) {
        return V8UNPACK_ERROR;
    }

    if (!IsV8File(pFileData, FileDataSize)) {
        return V8UNPACK_NOT_V8_FILE;
    }

	typedef Format15 format;

    char *InflateBuffer = nullptr;
    uint32_t InflateSize = 0;

	auto *pFileHeader = (format::file_header_t*) pFileData;

	format::block_header_t *pBlockHeader;

    pBlockHeader = (format::block_header_t*) &pFileHeader[1];
    memcpy(&FileHeader, pFileData, format::file_header_t::Size());


	format::elem_addr_t *pElemsAddrs = nullptr;
	auto ElemsAddrsSize = ReadBlockData<Format15>(pFileData, pBlockHeader, (char*&)pElemsAddrs);
    auto ElemsNum = ElemsAddrsSize / format::elem_addr_t::Size();

    Elems.clear();

	auto ret = V8UNPACK_OK;
    for (uint32_t i = 0; i < ElemsNum; i++) {

        if (pElemsAddrs[i].fffffff != format::elem_addr_t::UNDEFINED_VALUE) {
            ElemsNum = i;
            break;
        }

        pBlockHeader = (format::block_header_t*) &pFileData[pElemsAddrs[i].elem_header_addr + format::BASE_OFFSET];

        if (!pBlockHeader->IsCorrect()) {
            ret = V8UNPACK_HEADER_ELEM_NOT_CORRECT;
            break;
        }

		CV8Elem elem;
		elem.HeaderSize = ReadBlockData<Format15>(pFileData, pBlockHeader, elem.pHeader);

		//080228 Блока данных может не быть, тогда адрес блока данных равен 0x7fffffff
		if (pElemsAddrs[i].elem_data_addr != format::elem_addr_t::UNDEFINED_VALUE) {
			pBlockHeader = (format::block_header_t*) &pFileData[pElemsAddrs[i].elem_data_addr + format::BASE_OFFSET];
			elem.DataSize = ReadBlockData<Format15>(pFileData, pBlockHeader, elem.pData);
		} else
			elem.DataSize = ReadBlockData<Format15>(pFileData, nullptr, elem.pData);

		elem.UnpackedData.IsDataPacked = false;

		if (boolInflate && IsDataPacked) {

			int inflate_result = Inflate(elem.pData, &InflateBuffer, elem.DataSize, &InflateSize);

			if (inflate_result != Z_OK)
				IsDataPacked = false;
			else {
				elem.NeedUnpack = false; // отложенная распаковка не нужна
				delete[] elem.pData; //нераспакованные данные больше не нужны
				elem.pData = new char[InflateSize];
				elem.DataSize = InflateSize;
				memcpy(elem.pData, InflateBuffer, InflateSize);

				delete [] InflateBuffer;
				InflateBuffer = nullptr;
			}
		}

		if (IsV8File(elem.pData, elem.DataSize)) {
			ret = elem.UnpackedData.LoadFile(elem.pData, elem.DataSize, boolInflate);
			if (ret)
				break;

			delete [] elem.pData;
			elem.pData = nullptr;
			elem.IsV8File = true;
		}

		Elems.push_back(elem);

	} // for i = ..ElemsNum

	if (InflateBuffer)
		free(InflateBuffer);

	delete [] pElemsAddrs;

	return ret;
}

void CV8File::Dispose()
{
	std::vector<CV8Elem>::iterator elem;
	for (elem = Elems.begin(); elem != Elems.end(); ++elem) {
		elem->Dispose();
	}
	Elems.clear();
}

// Нѣкоторый условный предѣл
const size_t SmartLimit = 200 *1024;
const size_t SmartUnpackedLimit = 20 *1024*1024;

/*
	Лучше всѣго сжимается текст
	Берём степень сжатія текста в 99% (объём распакованных данных в 100 раз больше)
	Берём примѣрный порог использованія памяти в 20МБ (в этот объём должы влезть распакованные данные)
	Дѣлим 20МБ на 100 и получаем 200 КБ
	Упакованные данные размѣром до 200 КБ можно спокойно обрабатывать в памяти

	В дальнейшем этот показатель всё же будет вынесен в параметр командной строки
*/

template<typename format>
int SmartUnpack(std::basic_istream<char> &file, bool NeedUnpack, boost::filesystem::path &elem_path)
{

	typename format::block_header_t header;
	file.read((char*)&header, header.Size());

	int ret = 0;

	auto data_size = header.data_size();
	if (!NeedUnpack || data_size > SmartLimit) {
		/* 1) Имѣем дѣло с условно большими данными - работаем через промежуточный файл */
		/* 2) Не нужна распаковка - пишем прямо в файл-приёмник */

		boost::filesystem::ifstream src;

		boost::filesystem::path tmp_path = elem_path.parent_path() / ".v8unpack.tmp";
		boost::filesystem::path inf_path = elem_path.parent_path() / ".v8unpack.inf";
		boost::filesystem::path src_path;

		if (NeedUnpack) {
			/* Временный файл */

			boost::filesystem::ofstream out;

			out.open(tmp_path, std::ios_base::binary);
			ReadBlockData<format>(file, header, out);
			out.close();

			out.open(inf_path, std::ios_base::binary);
			boost::filesystem::ifstream inf(tmp_path, std::ios_base::binary);

			ret = Inflate(inf, out);

			if (ret) {
				// Файл не распаковывается - записываем, как есть
				inf.seekg(0, std::ios_base::beg);
				full_copy(inf, out);
			}

			inf.close();
			boost::filesystem::remove(tmp_path);
			out.close();

			src_path = inf_path;

		}
		else {
			/* Конечный файл */
			boost::filesystem::ofstream out;
			out.open(tmp_path, std::ios_base::binary);
			ReadBlockData<format>(file, header, out);
			out.close();

			src_path = tmp_path;
		}

		src.open(src_path, std::ios_base::binary);

		bool unpacked_as_V8 = false;

		if (IsV8File(src)) {
			vector<string> empty_filter;
			auto unpack_result = UnpackToDirectoryNoLoad(elem_path.string(), src, empty_filter, false, false);
			if (unpack_result == 0) {
				src.close();
				boost::filesystem::remove(src_path);
				unpacked_as_V8 = true;
			}
		}
		if (!unpacked_as_V8) {
			src.close();
			boost::system::error_code error;
			boost::filesystem::rename(src_path, elem_path, error);
		}


	}
	else {

		/* Имѣем полное право помѣстить файл в память */

		char *source_data = nullptr;
		auto uDataSize = ReadBlockData<format>(file, &header, source_data);

		char *out_data = nullptr;
		uint32_t out_data_size = 0;

		ret = Inflate(source_data, &out_data, uDataSize, &out_data_size);
		if (ret) {

			// файл не распаковывается - записываем, как есть
			out_data = source_data;
			out_data_size = uDataSize;

			source_data = nullptr;
		}
		delete[] source_data;

		bool unpacked_as_V8 = false;
		if (IsV8File(out_data, uDataSize)) {
			/* Это 8-файл - раскладываем его*/

			CV8File elem;
			auto unpack_result = elem.LoadFile(out_data, out_data_size, false, false);

			if (unpack_result == 0) {
				elem.SaveFileToFolder(elem_path.string());
				elem.Dispose();
				unpacked_as_V8 = true;
			}
		}
		if (!unpacked_as_V8) {
			/* Тупо пишем содержимое в цѣлевой файл*/

			boost::filesystem::ofstream out(elem_path, std::ios_base::binary);
			out.write(out_data, out_data_size);

		}

		free(out_data);

	}

	return ret;
}

static bool NameInFilter(const string &name, const vector<string> &filter)
{
	return filter.empty()
		|| find(filter.begin(), filter.end(), name) != filter.end();
}

template<typename format>
static int recursive_unpack(const string& directory, basic_istream<char>& file, const vector<string>& filter, bool boolInflate, bool UnpackWhenNeed)
{
	int ret = 0;

	boost::filesystem::path p_dir(directory);

	if (!boost::filesystem::exists(p_dir)) {
		if (!boost::filesystem::create_directory(directory)) {
			std::cerr << "UnpackToDirectoryNoLoad. Error in creating directory!" << std::endl;
			return ret;
		}
	}

	typename format::file_header_t FileHeader;

	std::ifstream::pos_type offset = format::BASE_OFFSET;
	file.seekg(offset);

	file.read((char*)& FileHeader, FileHeader.Size());

	typename format::block_header_t BlockHeader;
	typename format::block_header_t* pBlockHeader = &BlockHeader;

	file.read((char*)& BlockHeader, BlockHeader.Size());

	typename format::elem_addr_t* pElemsAddrs = nullptr;
	auto ElemsAddrsSize = ReadBlockData<format>(file, pBlockHeader, (char*&)pElemsAddrs);

	unsigned int ElemsNum = ElemsAddrsSize / format::elem_addr_t::Size();

	for (uint32_t i = 0; i < ElemsNum; i++) {

		if (pElemsAddrs[i].fffffff != format::UNDEFINED_VALUE) {
			ElemsNum = i;
			break;
		}

		file.seekg(pElemsAddrs[i].elem_header_addr + format::BASE_OFFSET, std::ios_base::beg);
		file.read((char*)& BlockHeader, sizeof(BlockHeader));

		if (!pBlockHeader->IsCorrect()) {
			ret = V8UNPACK_HEADER_ELEM_NOT_CORRECT;
			break;
		}

		CV8Elem elem;
		elem.HeaderSize = ReadBlockData<format>(file, pBlockHeader, elem.pHeader);
		string ElemName = elem.GetName();

		if (!NameInFilter(ElemName, filter)) {
			continue;
		}

		boost::filesystem::path elem_path(p_dir / ElemName);
		elem_path = boost::filesystem::absolute(elem_path);


		//080228 Блока данных может не быть, тогда адрес блока данных равен 0xffffffffffffffff
		if (pElemsAddrs[i].elem_data_addr != format::UNDEFINED_VALUE) {
			file.seekg(pElemsAddrs[i].elem_data_addr + format::BASE_OFFSET, std::ios_base::beg);
			SmartUnpack<format>(file, boolInflate /* && IsDataPacked*/, elem_path);
		}

		delete[] elem.pHeader;

	} // for i = ..ElemsNum

	delete[] pElemsAddrs;

	return ret;
}


int UnpackToDirectoryNoLoad(const string &directory, basic_istream<char> &file, const vector<string>  &filter, bool boolInflate, bool UnpackWhenNeed)
{
	if (!IsV8File(file)) {
		return V8UNPACK_NOT_V8_FILE;
	}

	if (IsV8File16(file)) {
		return recursive_unpack<Format16>(directory, file, filter, boolInflate, UnpackWhenNeed);
	}

	return recursive_unpack<Format15>(directory, file, filter, boolInflate, UnpackWhenNeed);
}


template<typename format>
static int list_files(boost::filesystem::ifstream &file)
{
	typename format::file_header_t FileHeader;

	file.seekg(format::BASE_OFFSET);
	file.read((char*)&FileHeader, FileHeader.Size());

	typename format::block_header_t BlockHeader;
	auto *pBlockHeader = &BlockHeader;

	file.read((char*)&BlockHeader, BlockHeader.Size());

	typename format::elem_addr_t *pElemsAddrs = nullptr;
	auto ElemsAddrsSize = ReadBlockData<format>(file, pBlockHeader, (char*&)pElemsAddrs);

	unsigned int ElemsNum = ElemsAddrsSize / format::elem_addr_t::Size();

	for (uint32_t i = 0; i < ElemsNum; i++) {
		if (pElemsAddrs[i].fffffff != format::UNDEFINED_VALUE) {
			ElemsNum = i;
			break;
		}

		file.seekg(pElemsAddrs[i].elem_header_addr + format::BASE_OFFSET, std::ios_base::beg);
		file.read((char*)&BlockHeader, BlockHeader.Size());

		if (!pBlockHeader->IsCorrect()) {
			continue;
		}

		CV8Elem elem;
		elem.HeaderSize = ReadBlockData<format>(file, pBlockHeader, elem.pHeader);

		string ElemName = elem.GetName();

		std::cout << ElemName << std::endl;
	}

	return V8UNPACK_OK;
}

int ListFiles(const std::string &filename)
{
	boost::filesystem::ifstream file(filename, std::ios_base::binary);

	if (!file) {
		std::cerr << "ListFiles `" << filename << "`. Input file not found!" << std::endl;
		return -1;
	}

	if (!IsV8File(file)) {
		return V8UNPACK_NOT_V8_FILE;
	}

	if (IsV8File16(file)) {
		return list_files<Format16>(file);
	}

	return list_files<Format15>(file);
}

template<typename format>
static int unpack_to_folder(boost::filesystem::ifstream &file, const std::string &dirname, const std::string &UnpackElemWithName, bool print_progress)
{
	int ret = V8UNPACK_OK;

	boost::filesystem::path p_dir(dirname);

	if (!boost::filesystem::exists(p_dir)) {
		if (!boost::filesystem::create_directory(dirname)) {
			std::cerr << "UnpackToDirectoryNoLoad. Error in creating directory!" << std::endl;
			return ret;
		}
	}

	typename format::file_header_t FileHeader;

	std::ifstream::pos_type offset = format::BASE_OFFSET;
	file.seekg(offset);
	file.read((char*)&FileHeader, FileHeader.Size());

	if (UnpackElemWithName.empty()) {
		boost::filesystem::path filename_out(dirname);
		filename_out /= "FileHeader";
		boost::filesystem::ofstream file_out(filename_out, std::ios_base::binary);
		file_out.write((char*)&FileHeader, sizeof(FileHeader));
		file_out.close();
	}

	typename format::block_header_t BlockHeader;
	typename format::block_header_t *pBlockHeader = &BlockHeader;

	file.read((char*)&BlockHeader, BlockHeader.Size());

	typename format::elem_addr_t *pElemsAddrs = nullptr;
	auto ElemsAddrsSize = ReadBlockData<format>(file, pBlockHeader, (char*&)pElemsAddrs);

	unsigned int ElemsNum = ElemsAddrsSize / format::elem_addr_t::Size();

	for (uint32_t i = 0; i < ElemsNum; i++) {

		if (pElemsAddrs[i].fffffff != format::UNDEFINED_VALUE) {
			ElemsNum = i;
			break;
		}

		file.seekg(pElemsAddrs[i].elem_header_addr + offset, std::ios_base::beg);
		file.read((char*)&BlockHeader, BlockHeader.Size());

		if (!pBlockHeader->IsCorrect()) {
			ret = V8UNPACK_HEADER_ELEM_NOT_CORRECT;
			break;
		}

		CV8Elem elem;
		elem.HeaderSize = ReadBlockData<format>(file, pBlockHeader, elem.pHeader);

		string ElemName = elem.GetName();

		// если передано имя блока для распаковки, пропускаем все остальные
		if (!UnpackElemWithName.empty() && UnpackElemWithName != ElemName) {
			continue;
		}

		boost::filesystem::path filename_out;
		boost::filesystem::ofstream file_out;

		filename_out = dirname;
		filename_out += "/";
		filename_out += ElemName;
		filename_out += ".header";

		file_out.open(filename_out, std::ios_base::binary);
		if (!file_out) {
			std::cerr << "UnpackToFolder. Error in creating file!" << std::endl;
			return -1;
		}
		file_out.write(reinterpret_cast<char*>(elem.pHeader), elem.HeaderSize);
		file_out.close();

		filename_out = dirname;
		filename_out += "/";
		filename_out += ElemName;
		filename_out += ".data";

		file_out.open(filename_out, std::ios_base::binary);
		if (!file_out) {
			std::cerr << "UnpackToFolder. Error in creating file!" << std::endl;
			return -1;
		}
		if (pElemsAddrs[i].elem_data_addr != V8_FF64_SIGNATURE) {

			file.seekg(pElemsAddrs[i].elem_data_addr + offset, std::ios_base::beg);

			typename format::block_header_t Header;
			file.read((char*)&Header, Header.Size());

			ReadBlockData<format>(file, Header, file_out);
		}
		file_out.close();

	}

	return 0;
}

int UnpackToFolder(const std::string &filename_in, const std::string &dirname, const std::string &UnpackElemWithName, bool print_progress)
{
	int ret = 0;

	boost::filesystem::ifstream file(filename_in, std::ios_base::binary);

	if (!file) {
		std::cerr << "UnpackToFolder. Input file not found!" << std::endl;
		return -1;
	}

	if (!IsV8File(file)) {
		return V8UNPACK_NOT_V8_FILE;
	}

	if (IsV8File16(file)) {
		return unpack_to_folder<Format16>(file, dirname, UnpackElemWithName, print_progress);
	}

	return unpack_to_folder<Format15>(file, dirname, UnpackElemWithName, print_progress);
}

template <typename format>
static bool checkV8File(std::basic_istream<char> &file)
{
	typename format::file_header_t FileHeader;
	typename format::block_header_t BlockHeader;

	memset(&BlockHeader, 0, sizeof(BlockHeader));

	std::ifstream::pos_type offset = file.tellg();

	file.seekg(format::BASE_OFFSET);
	file.read((char*)& FileHeader, FileHeader.Size());
	file.read((char*)& BlockHeader, BlockHeader.Size());

	file.seekg(offset);
	file.clear();

	return BlockHeader.IsCorrect();
}

template <typename format>
static bool checkV8File(const char *pFileData, uint32_t FileDataSize)
{
	if (!pFileData) {
		return false;
	}

	// проверим чтобы длина файла не была меньше длины заголовка файла и заголовка блока адресов
	if (FileDataSize < format::file_header_t::Size() + format::block_header_t::Size()) {
		return false;
	}

	auto *pBlockHeader = (const typename format::block_header_t*)&pFileData[format::file_header_t::Size()];

	return pBlockHeader->IsCorrect();
}


bool IsV8File(std::basic_istream<char> &file)
{
	return checkV8File<typename Format15>(file);
}

bool IsV8File16(std::basic_istream<char>& file)
{
	return checkV8File <Format16> (file);
}

bool IsV8File(const char *pFileData, uint32_t FileDataSize)
{
	return checkV8File <Format15> (pFileData, FileDataSize);
}

bool IsV8File16(const char *pFileData, uint32_t FileDataSize)
{
	return checkV8File <Format16> (pFileData, FileDataSize);
}

struct PackElementEntry {
	boost::filesystem::path  header_file;
	boost::filesystem::path  data_file;
	size_t                   header_size;
	size_t                   data_size;
};

int PackFromFolder(const std::string &dirname, const std::string &filename_out)
{
	boost::filesystem::path p_curdir(dirname);

	boost::filesystem::ofstream file_out(filename_out, std::ios_base::binary);
	if (!file_out) {
		std::cerr << "SaveFile. Error in creating file: " << filename_out << std::endl;
		return -1;
	}

	typedef stFileHeader file_header_t;
	typedef stBlockHeader block_header_t;
	typedef stElemAddr elem_addr_t;

	// записываем заголовок
	{
		boost::filesystem::ifstream file_in(p_curdir / "FileHeader", std::ios_base::binary);
		full_copy(file_in, file_out);
	}

	boost::filesystem::directory_iterator d_end;
	boost::filesystem::directory_iterator it(p_curdir);

	std::vector<PackElementEntry> Elems;

	for (; it != d_end; it++) {
		boost::filesystem::path current_file(it->path());
		if (current_file.extension().string() == ".header") {

			PackElementEntry elem;

			elem.header_file = current_file;
			elem.header_size = boost::filesystem::file_size(current_file);

			elem.data_file = current_file.replace_extension(".data");
			elem.data_size = boost::filesystem::file_size(elem.data_file);

			Elems.push_back(elem);

		}
	} // for it

	int ElemsNum = Elems.size();
	std::vector<elem_addr_t> ElemsAddrs;
	ElemsAddrs.reserve(ElemsNum);

	// cur_block_addr - смещение текущего блока
	// мы должны посчитать:
	//  + [0] заголовок файла
	//  + [1] заголовок блока с адресами
	//  + [2] размер самого блока адресов (не менее одной страницы?)
	//  + для каждого блока:
	//      + [3] заголовок блока метаданных (header)
	//      + [4] сами метаданные (header)
	//      + [5] заголовок данных
	//      + [6] сами данные (не менее одной страницы?)

	// [0] + [1]
	uint32_t cur_block_addr = file_header_t::Size() + block_header_t::Size();
	size_t addr_block_size = MAX(sizeof(elem_addr_t) * ElemsNum, V8_DEFAULT_PAGE_SIZE);
	cur_block_addr += addr_block_size; // +[2]

	for (auto elem : Elems) {
		elem_addr_t addr;

		addr.elem_header_addr = cur_block_addr;
		cur_block_addr += sizeof(block_header_t) + elem.header_size; // +[3]+[4]

		addr.elem_data_addr = cur_block_addr;
		cur_block_addr += sizeof(block_header_t); // +[5]
		cur_block_addr += MAX(elem.data_size, V8_DEFAULT_PAGE_SIZE); // +[6]

		addr.fffffff = V8_FF_SIGNATURE;

		ElemsAddrs.push_back(addr);
	}

	SaveBlockData(file_out, (char*) ElemsAddrs.data(), elem_addr_t::Size() * ElemsNum);

	for (auto elem : Elems) {

		boost::filesystem::ifstream header_in(elem.header_file, std::ios_base::binary);
		SaveBlockData(file_out, header_in, elem.header_size, elem.header_size);

		boost::filesystem::ifstream data_in(elem.data_file, std::ios_base::binary);
		SaveBlockData(file_out, data_in, elem.data_size, V8_DEFAULT_PAGE_SIZE);
	}

	file_out.close();

	return 0;
}

int SaveBlockData(std::basic_ostream<char> &file_out, std::basic_istream<char> &file_in, uint32_t BlockDataSize, uint32_t PageSize)
{
	if (PageSize < BlockDataSize)
		PageSize = BlockDataSize;

	typedef stFileHeader file_header_t;
	typedef stBlockHeader block_header_t;
	typedef stElemAddr elem_addr_t;

	block_header_t CurBlockHeader = block_header_t::create(BlockDataSize, PageSize, V8_FF_SIGNATURE);
	file_out.write(reinterpret_cast<char *>(&CurBlockHeader), sizeof(CurBlockHeader));
	full_copy(file_in, file_out);

	for(uint32_t i = 0; i < PageSize - BlockDataSize; i++) {
		file_out << (char)0;
	}

	return 0;
}

int SaveBlockData(std::basic_ostream<char> &file_out, const char *pBlockData, uint32_t BlockDataSize, uint32_t PageSize)
{
	if (PageSize < BlockDataSize)
		PageSize = BlockDataSize;

	typedef Format15 format;

	format::block_header_t CurBlockHeader = format::block_header_t::create(BlockDataSize, PageSize);
	file_out.write(reinterpret_cast<char *>(&CurBlockHeader), sizeof(CurBlockHeader));
	file_out.write(reinterpret_cast<const char *>(pBlockData), BlockDataSize);

	for(uint32_t i = 0; i < PageSize - BlockDataSize; i++) {
		file_out << (char)0;
	}

	return 0;
}

int Parse(const std::string &filename_in, const std::string &dirname, const std::vector< std::string > &filter)
{
    int ret = 0;

    boost::filesystem::ifstream file_in(filename_in, std::ios_base::binary);

    if (!file_in) {
        std::cerr << "Parse. `" << filename_in << "` not found!" << std::endl;
        return -1;
    }

    ret = UnpackToDirectoryNoLoad(dirname, file_in, filter);

    if (ret == V8UNPACK_NOT_V8_FILE) {
        std::cerr << "Parse. `" << filename_in << "` is not V8 file!" << std::endl;
        return ret;
    }

    std::cout << "Parse `" << filename_in << "`: ok" << std::endl << std::flush;

    return ret;
}

int CV8File::SaveFileToFolder(const boost::filesystem::path &directiory) const
{

    int ret = 0;

    if (!boost::filesystem::exists(directiory)) {
        ret = !boost::filesystem::create_directory(directiory);
        if (ret && errno == ENOENT) {
            std::cerr << "SaveFileToFolder. Error in creating directory `" << directiory << "` !" << std::endl;
            return ret;
        }
    }
    ret = 0;

    bool print_progress = true;
    uint32_t one_percent = Elems.size() / 50;
    if (print_progress && one_percent) {
        std::cout << "Progress (50 points): " << std::flush;
    }


    uint32_t ElemNum = 0;
    std::vector<CV8Elem>::const_iterator elem;
    for (elem = Elems.begin(); elem != Elems.end(); elem++) {

        ++ElemNum;
        if (print_progress && ElemNum && one_percent && ElemNum%one_percent == 0) {
            if (ElemNum % (one_percent*10) == 0)
                std::cout << "|" << std::flush;
            else
                std::cout << ".";
        }

        string ElemName = elem->GetName();

        boost::filesystem::path filename_out(directiory / ElemName);

        if (!elem->IsV8File) {
            boost::filesystem::ofstream file_out(filename_out, std::ios_base::binary);
            if (!file_out) {
                std::cerr << "SaveFile. Error in creating file!" << std::endl;
                return -1;
            }
            file_out.write(reinterpret_cast<char *>(elem->pData), elem->DataSize);
        } else {
            ret = elem->UnpackedData.SaveFileToFolder(filename_out);
            if (ret)
                break;
        }
    }

    if (print_progress && one_percent) {
        std::cout << std::endl << std::flush;
    }

    return ret;
}

string CV8Elem::GetName() const
{
	int ElemNameLen = (HeaderSize - CV8Elem::stElemHeaderBegin::Size()) / 2;
    string ElemName;
	for (int j = 0; j < ElemNameLen * 2; j += 2) {

        char currentChar = pHeader[CV8Elem::stElemHeaderBegin::Size() + j];
        if (currentChar != '\0') {
		    ElemName += currentChar;
        }

    }

	return ElemName;
}


int CV8Elem::SetName(const string &ElemName)
{
	uint32_t stElemHeaderBeginSize = CV8Elem::stElemHeaderBegin::Size();

	for (uint32_t j = 0; j < ElemName.size() * 2; j += 2, stElemHeaderBeginSize += 2) {
		pHeader[stElemHeaderBeginSize] = ElemName[j/2];
		pHeader[stElemHeaderBeginSize + 1] = 0;
	}

	return 0;
}

void CV8Elem::Dispose()
{
	if (pData != nullptr) {
		delete[] pData;
		pData = nullptr;
	}

	if (pHeader != nullptr) {
		delete[] pHeader;
		pHeader = nullptr;
	}

	IsV8File = false;
	HeaderSize = 0;
	DataSize = 0;
}


int CV8File::LoadFileFromFolder(const std::string &dirname)
{
	typedef Format15 format;

    FileHeader.next_page_addr = format::UNDEFINED_VALUE;
    FileHeader.page_size = format::DEFAULT_PAGE_SIZE;
    FileHeader.storage_ver = 0;
    FileHeader.reserved = 0;

    Elems.clear();

    boost::filesystem::directory_iterator d_end;
    boost::filesystem::directory_iterator dit(dirname);

    for (; dit != d_end; ++dit) {
        boost::filesystem::path current_file(dit->path());
        if (current_file.filename().string().at(0) == '.')
            continue;

		CV8Elem elem(current_file.filename().string());

		if (boost::filesystem::is_directory(current_file)) {

			elem.IsV8File = true;

			elem.UnpackedData.LoadFileFromFolder(current_file.string());
			elem.Pack(false);

        } else {
            elem.IsV8File = false;

            elem.DataSize = boost::filesystem::file_size(current_file);
            elem.pData = new char[elem.DataSize];

            boost::filesystem::ifstream file_in(current_file, std::ios_base::binary);
            file_in.read(reinterpret_cast<char *>(elem.pData), elem.DataSize);
        }

        Elems.push_back(elem);

    } // for directory_iterator

	return V8UNPACK_OK;
}

int BuildCfFile(const std::string &in_dirname, const std::string &out_filename, bool dont_deflate)
{
	//filename can't be empty
	if (in_dirname.empty()) {
		std::cerr << "Argument error - Set of `in_dirname' argument" << std::endl;
		return V8UNPACK_SHOW_USAGE;
	}

	if (out_filename.empty()) {
		std::cerr << "Argument error - Set of `out_filename' argument" << std::endl;
		return V8UNPACK_SHOW_USAGE;
	}

	typedef Format15 format;

	if (!boost::filesystem::exists(in_dirname)) {
		std::cerr << "Source directory does not exist!" << std::endl;
		return V8UNPACK_SOURCE_DOES_NOT_EXIST;
	}

	uint32_t ElemsNum = 0;
	{
		boost::filesystem::directory_iterator d_end;
		boost::filesystem::directory_iterator dit(in_dirname);

		for (; dit != d_end; ++dit) {

			boost::filesystem::path current_file(dit->path());
			std::string name = current_file.filename().string();

			if (name.at(0) == '.')
				continue;

			++ElemsNum;
		}
	}

	format::file_header_t FileHeader;

	//Предварительные расчеты длины заголовка таблицы содержимого TOC файла
	FileHeader.next_page_addr = format::UNDEFINED_VALUE;
	FileHeader.page_size = format::DEFAULT_PAGE_SIZE;
	FileHeader.storage_ver = 0;
	FileHeader.reserved = 0;
	auto cur_block_addr = format::file_header_t::Size() + format::block_header_t::Size();
	format::elem_addr_t *pTOC;
	pTOC = new format::elem_addr_t[ElemsNum];
	cur_block_addr += MAX(format::elem_addr_t::Size() * ElemsNum, format::DEFAULT_PAGE_SIZE);

	boost::filesystem::ofstream file_out(out_filename, std::ios_base::binary);
	//Открываем выходной файл контейнер на запись
	if (!file_out) {
		delete [] pTOC;
		std::cout << "SaveFile. Error in creating file!" << std::endl;
		return V8UNPACK_ERROR_CREATING_OUTPUT_FILE;
	}

	//Резервируем место в начале файла под заголовок и TOC
	for(unsigned i=0; i < cur_block_addr; i++) {
		file_out << '\0';
	}

	uint32_t one_percent = ElemsNum / 50;
	if (one_percent) {
		std::cout << "Progress (50 points): " << std::flush;
	}

	uint32_t ElemNum = 0;

	boost::filesystem::directory_iterator d_end;
	boost::filesystem::directory_iterator dit(in_dirname);
	for (; dit != d_end; ++dit) {

		boost::filesystem::path current_file(dit->path());
		std::string name = current_file.filename().string();

		if (name.at(0) == '.')
			continue;

		//Progress bar ->
		{
			if (ElemNum && one_percent && ElemNum%one_percent == 0) {
				if (ElemNum % (one_percent*10) == 0)
					std::cout << "|" << std::flush;
				else
					std::cout << ".";
			}
		}//<- Progress bar

		CV8Elem pElem;

		pElem.HeaderSize = CV8Elem::stElemHeaderBegin::Size() + name.size() * 2 + 4; // последние четыре всегда нули?
		pElem.pHeader = new char[pElem.HeaderSize];

		memset(pElem.pHeader, 0, pElem.HeaderSize);

		pElem.SetName(name);

		pTOC[ElemNum].elem_header_addr = file_out.tellp() - format::BASE_OFFSET;
		SaveBlockData(file_out, pElem.pHeader, pElem.HeaderSize, pElem.HeaderSize);

		pTOC[ElemNum].elem_data_addr = file_out.tellp() - format::BASE_OFFSET;
		pTOC[ElemNum].fffffff = format::UNDEFINED_VALUE;

		if (boost::filesystem::is_directory(current_file)) {

			pElem.IsV8File = true;

			std::string new_dirname(in_dirname);
			new_dirname += "/";
			new_dirname += name;

			pElem.UnpackedData.LoadFileFromFolder(new_dirname);
			pElem.Pack(!dont_deflate);

			SaveBlockData(file_out, pElem.pData, pElem.DataSize);

		} else {

			pElem.IsV8File = false;

			pElem.DataSize = boost::filesystem::file_size(current_file);

			boost::filesystem::path p_filename(in_dirname);
			p_filename /= name;
			boost::filesystem::ifstream file_in(p_filename, std::ios_base::binary);

			if (pElem.DataSize < SmartUnpackedLimit) {

				pElem.pData = new char[pElem.DataSize];
				file_in.read(reinterpret_cast<char*>(pElem.pData), pElem.DataSize);
				pElem.Pack(!dont_deflate);
				SaveBlockData(file_out, pElem.pData, pElem.DataSize);

			} else {

				if (dont_deflate) {
					SaveBlockData(file_out, file_in, pElem.DataSize);
				} else {
					// Упаковка через промежуточный файл
					boost::filesystem::path tmp_file_path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

					{
						boost::filesystem::ofstream tmp_file(tmp_file_path, std::ios_base::binary);
						Deflate(file_in, tmp_file);
						tmp_file.close();
					}

					{
						pElem.DataSize = boost::filesystem::file_size(tmp_file_path);
						boost::filesystem::ifstream tmp_file(tmp_file_path, std::ios_base::binary);
						SaveBlockData(file_out, tmp_file, pElem.DataSize);
						tmp_file.close();

						boost::filesystem::remove(tmp_file_path);
					}
				}
			}
		}

		pElem.Dispose();

		ElemNum++;
	}

	//Записывем заголовок файла
	file_out.seekp(0, std::ios_base::beg);
	file_out.write(reinterpret_cast<const char*>(&FileHeader), format::file_header_t::Size());

	//Записываем блок TOC
	SaveBlockData(file_out, (const char*) pTOC, format::elem_addr_t::Size() * ElemsNum);

	delete [] pTOC;

	std::cout << std::endl << "Build `" << out_filename << "` OK!" << std::endl << std::flush;

	return V8UNPACK_OK;
}

int CV8Elem::Pack(bool deflate)
{
	int ret = 0;
	if (!IsV8File) {

		if (deflate) {

			char *DeflateBuffer = nullptr;
			uint32_t DeflateSize = 0;

			ret = Deflate(pData, &DeflateBuffer, DataSize, &DeflateSize);
			if (ret) {
				return ret;
			}

			delete[] pData;
			pData = new char[DeflateSize];
			DataSize = DeflateSize;
			memcpy(pData, DeflateBuffer, DeflateSize);

			delete [] DeflateBuffer;
		}

	} else {

		char *DataBuffer = nullptr;
		uint32_t DataBufferSize = 0;

		UnpackedData.GetData(&DataBuffer, &DataBufferSize);
		UnpackedData.Dispose();

		if (deflate) {

			char *DeflateBuffer = nullptr;
			uint32_t DeflateSize = 0;

			ret = Deflate(DataBuffer, &DeflateBuffer, DataBufferSize, &DeflateSize);
			if (ret) {
				return ret;
			}

			pData = new char[DeflateSize];
			DataSize = DeflateSize;
			memcpy(pData, DeflateBuffer, DeflateSize);

			delete [] DeflateBuffer;

		} else {

			pData = new char[DataBufferSize];
			DataSize = DataBufferSize;
			memcpy(pData, DataBuffer, DataBufferSize);

		}

		delete [] DataBuffer;

		IsV8File = false;
	}

	return V8UNPACK_OK;
}

int CV8File::Pack()
{
    char *DeflateBuffer = nullptr;
    uint32_t DeflateSize = 0;

    char *DataBuffer = nullptr;
    uint32_t DataBufferSize = 0;

    int ret = 0;

    bool print_progress = true;
    uint32_t ElemsNum = Elems.size();
    uint32_t one_percent = ElemsNum / 50;
    if (print_progress && one_percent) {
        std::cout << "Progress (50 points): " << std::flush;
    }


    uint32_t ElemNum = 0;

    for (auto elem : Elems) {

        ++ElemNum;
        if (print_progress && ElemNum && one_percent && ElemNum%one_percent == 0) {
            if (ElemNum % (one_percent*10) == 0)
                std::cout << "|" << std::flush;
            else
                std::cout << ".";
        }

        if (!elem.IsV8File) {
            ret = Deflate(elem.pData, &DeflateBuffer, elem.DataSize, &DeflateSize);
            if (ret)
                return ret;

            delete[] elem.pData;
            elem.pData = new char[DeflateSize];
            elem.DataSize = DeflateSize;
            memcpy(elem.pData, DeflateBuffer, DeflateSize);

            delete [] DeflateBuffer;
            DeflateBuffer = nullptr;

        } else {
            elem.UnpackedData.GetData(&DataBuffer, &DataBufferSize);

            ret = Deflate(DataBuffer, &DeflateBuffer, DataBufferSize, &DeflateSize);
            if (ret)
                return ret;

			delete [] DataBuffer;
			DataBuffer = nullptr;

            elem.IsV8File = false;

            elem.pData = new char[DeflateSize];
            elem.DataSize = DeflateSize;
            memcpy(elem.pData, DeflateBuffer, DeflateSize);

            delete [] DeflateBuffer;
            DeflateBuffer = nullptr;

        }
    }

    if (print_progress && one_percent) {
        std::cout << std::endl;
    }

    return V8UNPACK_OK;
}

int CV8File::GetData(char **DataBuffer, uint32_t *DataBufferSize)
{
	typedef Format15 format;

	uint32_t ElemsNum = Elems.size();

	auto NeedDataBufferSize = format::file_header_t::Size();

	// заголовок блока и данные блока - адреса элементов с учетом минимальной страницы 512 байт
	NeedDataBufferSize += format::block_header_t::Size() + MAX(format::elem_addr_t::Size() * ElemsNum, format::DEFAULT_PAGE_SIZE);

	for (auto elem : Elems) {

		// заголовок блока и данные блока - заголовок элемента
		NeedDataBufferSize += format::block_header_t::Size()  + elem.HeaderSize;

		if (elem.IsV8File) {
			elem.UnpackedData.GetData(&elem.pData, &elem.DataSize);
			elem.IsV8File = false;
		}
		NeedDataBufferSize += format::block_header_t::Size() + MAX(elem.DataSize, format::DEFAULT_PAGE_SIZE);
	}

    // Создаем и заполняем данные по адресам элементов
	format::elem_addr_t *pTempElemsAddrs = new format::elem_addr_t[ElemsNum], *pCurrentTempElem;
	pCurrentTempElem = pTempElemsAddrs;

	auto cur_block_addr = format::file_header_t::Size() + format::block_header_t::Size();
	cur_block_addr += MAX(format::elem_addr_t::Size() * ElemsNum, format::DEFAULT_PAGE_SIZE);

	for (auto elem : Elems) {

		pCurrentTempElem->elem_header_addr = cur_block_addr;
		cur_block_addr += format::block_header_t::Size() + elem.HeaderSize;

		pCurrentTempElem->elem_data_addr = cur_block_addr;
		cur_block_addr += format::block_header_t::Size();

		cur_block_addr += MAX(elem.DataSize, V8_DEFAULT_PAGE_SIZE);

		pCurrentTempElem->fffffff = format::UNDEFINED_VALUE;
		++pCurrentTempElem;
	}

	*DataBuffer = static_cast<char*> (realloc(*DataBuffer, NeedDataBufferSize));
	char *cur_pos = *DataBuffer;

	// записываем заголовок
	memcpy(cur_pos, (char*) &FileHeader, format::file_header_t::Size());
	cur_pos += format::file_header_t::Size();

	// записываем адреса элементов
	SaveBlockDataToBuffer<format>(&cur_pos, (char*) pTempElemsAddrs, format::elem_addr_t::Size() * ElemsNum);

	// записываем элементы (заголовок и данные)
	for (auto elem : Elems) {
		SaveBlockDataToBuffer<format>(&cur_pos, elem.pHeader, elem.HeaderSize, elem.HeaderSize);
		SaveBlockDataToBuffer<format>(&cur_pos, elem.pData, elem.DataSize);
	}

	if (pTempElemsAddrs)
		delete[] pTempElemsAddrs;

	*DataBufferSize = NeedDataBufferSize;

	return V8UNPACK_OK;
}

stBlockHeader stBlockHeader::create(uint32_t block_data_size, uint32_t page_size)
{
	return create(block_data_size, page_size, UNDEFINED_VALUE);
}

stBlockHeader stBlockHeader::create(uint32_t block_data_size, uint32_t page_size, uint32_t next_page_addr)
{
	stBlockHeader BlockHeader;
	BlockHeader.set_data_size(block_data_size);
	BlockHeader.set_page_size(page_size);
	BlockHeader.set_next_page_addr(next_page_addr);
	return BlockHeader;
}

stBlockHeader64 stBlockHeader64::create(uint64_t block_data_size, uint64_t page_size)
{
	return create(block_data_size, page_size, UNDEFINED_VALUE);
}

stBlockHeader64 stBlockHeader64::create(uint64_t  block_data_size, uint64_t  page_size, uint64_t next_page_addr)
{
	stBlockHeader64 BlockHeader;
	BlockHeader.set_data_size(block_data_size);
	BlockHeader.set_page_size(page_size);
	BlockHeader.set_next_page_addr(next_page_addr);
	return BlockHeader;
}

}