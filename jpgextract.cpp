#include <io.h>
#include <filesystem>
#include <iostream>
#include <Windows.h>
#include <iostream>
#include <sstream>
#include <vector>

#define MAXBUFFSIZE 10240000

struct MBR 
{
	// BIOS PARAMETERS BLOCK
	unsigned short bytes_per_sector;
	unsigned char sectors_per_cluster;
	unsigned short  reserved_sectors_count;
	unsigned char fat_count;
	unsigned short root_entries_maxnum;
	unsigned short sector_count;
	unsigned int hidden_sector_count;
	unsigned int _sectors_per_fat;
	unsigned int _root_dir_clusterindex;

};
struct DIR_ENTRY
{
	unsigned char _mark;
	unsigned char _name[8];
	unsigned char _ext[3];
	unsigned char _flag;
	unsigned short _clusterindex;
	unsigned int _filesize;

};

bool isJPEGMAGICNUMBER_B(unsigned char* data);
bool isJPEGMAGICNUMBER_L(unsigned char* data);
bool isBMPMAGICNUMBER_B(unsigned char* data);
uint32_t BytesToUint(unsigned char* arr);
bool TryDecodingBMP(unsigned char* data);
uint16_t BytesToShort(unsigned char* arr);
bool isJPEGALL(unsigned char* data);
int GetJPEGByteSize(unsigned char* data, int dtend);



unsigned char MEGABUFFER[51200000];


int PrintRootDirInfo(unsigned char* data);
void IncrementallyReadDirectories(HANDLE* h, int off);
bool GetDirEntry(DIR_ENTRY* d, unsigned char* data);
void PrintDirEntry(DIR_ENTRY* d);

unsigned int Get_Next_Cluster(HANDLE* h, int fatoff, unsigned int current_cluster);
unsigned char* Get_All_Cluster(HANDLE* h, int fatoff, unsigned int starting_cluster);
void ReadFAT_TABLE(HANDLE* h, int fatoff);
void Search_Cluster_In_Table(HANDLE* h, int fatoff, int clusternum);
void CopyCluster(HANDLE* h, unsigned int cluster, unsigned char* data, int dtOffset);
int bpb_sector = 0;
MBR master_boot_record;
void ReadFATSectorFromCluster(HANDLE* h, int fatoff, int cluster);

bool GetDirEntry(DIR_ENTRY* d, unsigned char* data) 
{
	d->_mark = data[0];
	memcpy(d->_name, data, 8);
	memcpy(d->_ext, data + 0x08, 3);
	d->_clusterindex = BytesToShort(data + 0x1A);
	d->_filesize = BytesToUint(data + 0x1C);
	d->_flag = data[11];
	return true;
}


void IncrementallyReadDirectories(HANDLE * h, int off ) 
{
	int bOffset = off;
	DWORD readBytes = 0;

	unsigned char buffer[512];

	SetFilePointer(*h, bOffset, NULL, FILE_BEGIN);
	ReadFile(*h, buffer, sizeof(buffer), &readBytes, NULL);
	int doffset = 0;
	// read root dir up to last entry
	DIR_ENTRY d; 

	while (1) {
		GetDirEntry(&d, buffer + doffset);
		//PrintDirEntry(&d);
		if (d._mark == 0x00) {
			break;
		}
		if (d._mark != 0xE5) {
			if (d._flag == 0x10 && d._name[0] != '.') {
				int nOffset = ((bpb_sector + master_boot_record.reserved_sectors_count + (master_boot_record.fat_count * master_boot_record._sectors_per_fat))
					* master_boot_record.bytes_per_sector) + (d._clusterindex - 2) * (master_boot_record.sectors_per_cluster * master_boot_record.bytes_per_sector);
				IncrementallyReadDirectories(h, nOffset);
			}
			if (d._flag == 0x20) {
				//std::cout << "File found. First cluster at " << d._clusterindex << std::endl;
			}
		}
		
		doffset += 32;
	

	}
}

void Search_Cluster_In_Table(HANDLE* h, int fatoff, int clusternum) 
{
	int i = 0;
	int off = 0;
	unsigned char sectordata[512];
	while (i < master_boot_record._sectors_per_fat) {
		DWORD readBytes = 0;
		SetFilePointer(*h, fatoff + off, NULL, FILE_BEGIN);
		ReadFile(*h, sectordata, sizeof(sectordata), &readBytes, NULL);
		if (readBytes == 0) {
			std::cout << "Error reading disk...";
			break;
		}
		// now read every 4 bytes sec (so 128 times) 
		for (int n = 0; n < 128; n++) {
			unsigned char fatdata[4];
			memcpy(fatdata, sectordata + (n * 4), 4);
			//mask first byte 
			fatdata[0] = 0;
			unsigned int v = BytesToUint(fatdata);
			
			if (v >= 0x02 && v <= 0xFFFFFEF) {
				if (v == clusternum) 
				{
					//std::cout << "Entry cluster found at sector #" << i << ". Fat entry # " << n << std::endl;
					return;
				}
			}

		}
		off += 512;
		i++;
	}
}


void CopyCluster(HANDLE* h, unsigned int cluster, unsigned char* data, int dtOffset )
{

	int stoffset = ((bpb_sector + master_boot_record.reserved_sectors_count + (master_boot_record.fat_count * master_boot_record._sectors_per_fat))
		* master_boot_record.bytes_per_sector) + (cluster - 2) * (master_boot_record.sectors_per_cluster * master_boot_record.bytes_per_sector);
	DWORD readBytes = 0;

	int off = 0;
	for (int i = 0; i < master_boot_record.sectors_per_cluster; i++) {

		SetFilePointer(*h, stoffset+off, NULL, FILE_BEGIN);
		ReadFile(*h, data + dtOffset + off, 512, &readBytes, NULL);
		off += 512;
	}
	
}
unsigned char* Get_All_Cluster(HANDLE* h, int fatoff, unsigned int starting_cluster) 
{
	unsigned int lastcluster = starting_cluster;
	int clusternum = 1;
	while (true) 
	{
		unsigned int r = Get_Next_Cluster(h, fatoff, lastcluster);
		if (r == 0x00) {
			return NULL;
		}
		else if (r >= 0x02 && r <= 0xFFFFFEF) {
			lastcluster = r;

		}
		else if (r == 0xFFFFFF7) {
			return NULL;
		}
		else if (r >= 0xFFFFFF8 && r <= 0xFFFFFFF) 
		{
			break;
		}
		clusternum++;
	}

	unsigned char* result = (unsigned char*)malloc((clusternum * master_boot_record.sectors_per_cluster * master_boot_record.bytes_per_sector));
	memcpy(result, &clusternum, 4);
	int boff = 4;
	lastcluster = starting_cluster;
	while (true)
	{

		unsigned int r = Get_Next_Cluster(h, fatoff, lastcluster);
		if (r == 0x00) {
			return NULL;
		}
		else if (r >= 0x02 && r <= 0xFFFFFEF) {
			
			memcpy(result + boff, &lastcluster, 4);
			lastcluster = r;

		}
		else if (r == 0xFFFFFF7) {
			return NULL;
		}
		else if (r >= 0xFFFFFF8 && r <= 0xFFFFFFF) 
		{
			memcpy(result + boff, &lastcluster, 4);
			break;
		}
		boff += 4;
	}
	return result; 

}
unsigned int Get_Next_Cluster(HANDLE* h, int fatoff, unsigned int current_cluster) 
{

	int sectoroffset = (current_cluster * 4) / master_boot_record.bytes_per_sector;
	int entry_index = current_cluster - (sectoroffset * 128);
	int entry_off = entry_index * 4; // 4 bytes per entry
	//std::cout << "Cluster asked : " << current_cluster << std::endl;
	//std::cout << "Sector offset from cluster : " << sectoroffset << std::endl;
	//std::cout << "Entry index from cluster sector : " << entry_index << std::endl;
	DWORD readBytes = 0;
	unsigned char sectordata[512];
	SetFilePointer(*h, fatoff + (sectoroffset* master_boot_record.bytes_per_sector), NULL, FILE_BEGIN);
	ReadFile(*h, sectordata, sizeof(sectordata), &readBytes, NULL);
	if (readBytes == 0) {
		std::cout << "Error reading disk...";
		return 0;
	}
	unsigned char fatdata[4];
	memcpy(fatdata, sectordata + entry_off, 4);

	fatdata[3] &= 0x0F; // Clear out the first nibble
	unsigned int v = BytesToUint(fatdata);
	
	if (v == 0x00) 
	{
		//std::cout << "Empty cluster" << std::endl;
		
	}
	else if (v >= 0x02 && v <= 0xFFFFFEF) 
	{
		//std::cout << "Cluster used at " << v << std::endl;
	}
	else if (v == 0xFFFFFF7) {
		//std::cout << "Bad cluster" << std::endl;
	}

	else if (v >= 0xFFFFFF8 && v <= 0xFFFFFFF) {
		//std::cout << "Cluster used. End of file." << std::endl;
	}
	return v;
}
void ReadFATSectorFromCluster(HANDLE* h, int fatoff, int cluster)
{
	int sectoroffset = (cluster * 4) / master_boot_record.bytes_per_sector;
	fatoff += (sectoroffset * master_boot_record.bytes_per_sector); 
	ReadFAT_TABLE(h, fatoff);
}
void ReadFAT_TABLE(HANDLE* h, int fatoff)
{
	// read first sec
	int i = 0;
	int off = 0;
	unsigned char sectordata[512];
	while (i < 1) {// master_boot_record._sectors_per_fat) {
		DWORD readBytes = 0;
		SetFilePointer(*h, fatoff + off, NULL, FILE_BEGIN);
		ReadFile(*h, sectordata, sizeof(sectordata), &readBytes, NULL);
		if (readBytes == 0) 
		{
			std::cout << "Error reading disk...";
			break;
		}
		// now read every 4 bytes sec (so 128 times) 
		for (int n = 0; n < 128; n++) {
			unsigned char fatdata[4];
			memcpy(fatdata, sectordata + (n * 4), 4);

			fatdata[3] &= 0x0F; // Clear out the first nibble
			unsigned int v = BytesToUint(fatdata);
			if (v == 0x00) {
				//std::cout << "Empty cluster" << std::endl;
			}
			else if (v >= 0x02 && v <= 0xFFFFFEF) {
				//std::cout << "Cluster used at " << v << std::endl;

			}
			else if (v == 0xFFFFFF7) {
				//std::cout << "Bad cluster" << std::endl;
			}
			//								0xFFFFFFF
			//                              0xFFFFFF
			else if (v >= 0xFFFFFF8 && v <= 0xFFFFFFF) {
				//std::cout << "Cluster used. End of file." << std::endl;
			}

		}
		off += 512;
		i++;
	}
}


int main(int argv, char** args)
{
	char inp[255];

	std::cout << "[Type corrupted drive letter]" << std::endl;
	std::cin.getline(inp, 256);
	std::wstringstream s;
	s << "\\\\.\\" << inp << ":";

	HANDLE h = CreateFile(s.str().c_str(), (GENERIC_READ | GENERIC_WRITE), FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	unsigned char buffer[512];
	int cntrA = 0;
	int cntrB = 0;
	FILE* f;
	LONG bOffset = 0;
	DWORD readBytes = 0; 
	int r_counter = 0;

	std::cout << " Start reading sectors..." << std::endl;
	//  [1] READ PARAMETERS BLOCK IN BOOT SECTOR
	std::cout << " Finding master boot record..." << std::endl;
	while ( true ){
	
		// Search 
		readBytes = 0;
		SetFilePointer(h, bOffset, NULL, FILE_BEGIN);
		ReadFile(h, buffer, sizeof(buffer), &readBytes, NULL);
		bOffset += 512;
		
		// read BIOS PARAMETERS BLOCK [offset 0x00b] inside first sector. 
		
		if (buffer[0x1FE] == 0x55 && buffer[0x1FF] == 0xAA) 
		{
			std::cout << "Bootloader signature found at sector #" << bpb_sector <<"."  << std::endl;
			master_boot_record.bytes_per_sector = BytesToShort(buffer + 0x0B);
			master_boot_record.sectors_per_cluster = buffer[0x0D];
			master_boot_record.reserved_sectors_count = BytesToShort(buffer + 0x0E);
			master_boot_record.fat_count = buffer[0x10];
			master_boot_record.root_entries_maxnum = BytesToShort(buffer + 0x11);
			master_boot_record.sector_count = BytesToShort(buffer + 0x13);
			master_boot_record.hidden_sector_count= BytesToUint(buffer + 0x1C);
			master_boot_record._sectors_per_fat = BytesToUint(buffer + 0x24);
			master_boot_record._root_dir_clusterindex = BytesToUint(buffer + 0x2c);

			// 0x036 	0x2B 	8
			char sys_type[8]; 
			memcpy(sys_type, buffer + 0x28, 8);
			std::cout << "System : ";
			for (int i = 0; i < 8; i++) {
				printf("%c", sys_type[i]);
			}
			std::cout << std::endl;

			if (BytesToShort(buffer + 0x11) == 0) {
				std::cout << "FAT32 unreadable." << std::endl;
			}
			break;
		}
		bpb_sector++;
		
	}
	// Show some Master boot record info 

	std::cout << "Number of bytes per logical sector (square root) : " << (int)master_boot_record.bytes_per_sector << std::endl;
	std::cout << "Number of sectors per cluster                    : " << (int)master_boot_record.sectors_per_cluster << std::endl;
	std::cout << "Number of reserved sectors                       : " << (int)master_boot_record.reserved_sectors_count << std::endl;
	std::cout << "Number of file alloc tables                      : " << (int)master_boot_record.fat_count << std::endl;
	std::cout << "Number of  hidden sectors                        : " << (int)master_boot_record.hidden_sector_count << std::endl;
	std::cout << "Sectors per fat                                  : " << (int)master_boot_record._sectors_per_fat << std::endl;
	std::cout << "Root dir cluster index                           : " << (int)master_boot_record._root_dir_clusterindex<< std::endl;

	
	int rootOffset = (bpb_sector + master_boot_record.reserved_sectors_count + (master_boot_record.fat_count * master_boot_record._sectors_per_fat)) 
		* master_boot_record.bytes_per_sector;

	IncrementallyReadDirectories(&h, rootOffset);
	
	// Get File table pointer
	int fatOffset = (master_boot_record.reserved_sectors_count) * master_boot_record.bytes_per_sector;
	ReadFAT_TABLE(&h, fatOffset);
	unsigned char* all_c = Get_All_Cluster(&h, fatOffset, 7768);
	if (all_c == NULL) {
		//std::cout << "Error" << std::endl;
	}
	else {
		// Fat table is not corrupted.
		uint32_t clusternumber = BytesToUint(all_c);
		int filesize = clusternumber * master_boot_record.bytes_per_sector * master_boot_record.sectors_per_cluster;
		unsigned char* extractdata = (unsigned char*)malloc(filesize);
		//std::cout << "Number of clusters used for this file : " << clusternumber << std::endl;
		//std::cout << "Binary size of the file : " << filesize << std::endl;
		int extractoff = 0;
		for (int i = 0; i < clusternumber; i++) 
		{
			//std::cout << "Cluster index: " << BytesToUint(all_c + 4 + (i * 4)) << std::endl;
			CopyCluster(&h, BytesToUint(all_c + 4 + (i * 4)), extractdata, extractoff);
			extractoff += master_boot_record.bytes_per_sector * master_boot_record.sectors_per_cluster;
		}
		
		GetJPEGByteSize(extractdata, filesize);
		f = fopen("extractraw.jpeg", "ab");
		if (f == NULL) { return 0; } // throw error if cannot read
		fseek(f, 0, SEEK_END);
		fwrite(extractdata, 1, clusternumber * master_boot_record.bytes_per_sector * master_boot_record.sectors_per_cluster, f);
		fclose(f);
		//std::cout << "File extracted!!! ";
		free(extractdata);
		free(all_c);
	}

	// Reading Disk

	bOffset = 0;
	std::cout << "Reading Hard drive..." << std::endl;

	while (true) 
	{
		readBytes = 0;
		SetFilePointer(h, bOffset, NULL, FILE_BEGIN);
		ReadFile(h, buffer, sizeof(buffer), &readBytes, NULL);

		if (readBytes == 0) 
		{
			//std::cout << "Something went wrong....";
			break;
		}
	
		if (isJPEGALL(buffer)) 
		{
			// Jpeg was found.

			if (r_counter < 186) {
				r_counter++;
			}
			else 
			{
				int tempoffset = bOffset;
				for (int i = 0; i < 20000; i++) {
					SetFilePointer(h, tempoffset, NULL, FILE_BEGIN);
					ReadFile(h, MEGABUFFER + (i * 512), sizeof(buffer), &readBytes, NULL); // read it as it is 
					tempoffset += 512;
				}
				std::cout << "Searching jpeg file size..." << std::endl;
				int jpsize = GetJPEGByteSize(MEGABUFFER, MAXBUFFSIZE);
				std::cout << "JPEG size is " << jpsize << " bytes. " << std::endl;

				if (jpsize < MAXBUFFSIZE) {
					std::cout << "Start extracting ... ";
					unsigned char* extractstuff = (unsigned char*)malloc(jpsize);
					memcpy(extractstuff, MEGABUFFER, jpsize);

					std::ostringstream s;
					s << "extract\\out_" << r_counter << ".jpeg";

					f = fopen(s.str().c_str(), "ab");
					if (f == NULL) { return 0; } // throw error if cannot read
					fseek(f, 0, SEEK_END);
					fwrite(extractstuff, 1, jpsize, f);
					fclose(f);
					free(extractstuff);
					std::cout << "File extracted at extract/out_" << r_counter << ".jpeg" << std::endl;
					r_counter++;
				}
			}
		}


		bOffset += 512; // read next sector
		cntrA++;
		if (cntrA == 195400) {
			cntrB += 100;
			cntrA = 0;
			std::cout << cntrB << "MBs scanned..." << std::endl;
		}
	}
	std::cout << "Extraction done. You can close the software.";
	CloseHandle(h);
	while (1) {}
}


bool TryDecodingBMP(unsigned char* data)
{
	int bmpsize = BytesToUint(data + 2);
	int bitoffset = BytesToUint(data + 10);
	int bisize = BytesToUint(data + 14);
	int biWidth = BytesToUint(data + 18);
	int biHeight = BytesToUint(data + 22);

	short biPlanes = BytesToShort(data + 26);
	short biBitCount = BytesToShort(data + 28); //  Bits color per pixel
	int biCompression = BytesToUint(data + 30);
	int biSizeImage = BytesToUint(data + 34);
	int biXpelsPerMeter = BytesToUint(data + 38);
	int biYpelsPerMeter = BytesToUint(data + 42);
	int biClrUsed = BytesToUint(data + 46);
	int biClrImportant = BytesToUint(data + 50);
	

	return true;

}

bool isBMPMAGICNUMBER_B(unsigned char* data) {
	if (data[0] == 0x42 && data[1] == 0x4D) {
		return true;
	}
}
bool isJPEGALL(unsigned char* data)
{
	// jpeg200 : 
	//00 00 00 0C 6A 50 20 20 0D 0A 87 0A
	char jpg2k1[12] = { 0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A };  // 12 bytes 
	char jpg2k2[4] = { 0xFF, 0x4F, 0xFF, 0x51 };

	// jpeg raw
	// 76 2F 31 01
	char jpraw1[4] = { 0x76, 0x2F, 0x31, 0x01 };
	char jpraw2[4] = { 0x42, 0x50, 0x47, 0xFB };
	// 42 50 47 FB
	char jpraw3[4] = { 0xFF, 0xD8, 0xFF, 0xDB };
	// FF D8 FF DB
	char jpraw4[12] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01 };
	// FF D8 FF E0 00 10 4A 46 49 46 00 01
	char jpraw5[5] = { 0xFF, 0xD8, 0xFF, 0xEE };
	// FF D8 FF EE
	// JPEG XL
	//00 00 00 0C 4A 58 4C 20 0D 0A 87 0A
	char jpXL[12] = { 0x00, 0x00, 0x00, 0x0C, 0x4A, 0x58, 0x4C, 0x20, 0x0D, 0x0A, 0x87, 0x0A };
	// FF 0A

	if (memcmp(data, jpg2k1, 12) == 0) {
		std::cout << ".JPG2K1 format found..." << std::endl; return true;
	}
	if (memcmp(data, jpraw4, 12) == 0) {
		std::cout << ".jpraw4 format found..." << std::endl; return true;
	}
	if (memcmp(data, jpXL, 12) == 0) {
		std::cout << ".jpXL format found..." << std::endl; return true;
	}

	if (memcmp(data, jpg2k2, 4) == 0) {
		std::cout << ".jpg2k2 format found..." << std::endl; return true;
	}
	if (memcmp(data, jpraw1, 4) == 0) {
		std::cout << ".jpraw1 format found..." << std::endl; return true;
	}
	if (memcmp(data, jpraw2, 4) == 0) {
		std::cout << ".jpraw2 format found..." << std::endl; return true;
	}
	if (memcmp(data, jpraw3, 4) == 0) {
		std::cout << ".jpraw3 format found..." << std::endl; return true;
	}
	if (memcmp(data, jpraw5, 4) == 0) {
		std::cout << ".jpraw5 format found..." << std::endl; return true;
	}
	return false;
}
int GetJPEGByteSize(unsigned char* data, int dtend) 
{
	//std::cout << "Magic word : " << (int)data[0] << " " << (int)data[1];
	int off = 2;
	while (off < dtend) {
		unsigned char c1 = data[off]; off++;
		if (c1 == 0xff) {
			unsigned char c2 = data[off]; off++;
			if (c2 == 0xD9) 
			{
				//std::cout << "EOI found at current offset " << off << std::endl;
				return off;
			}
			if (c2 == 0xDA) {
				//SOS SEGMENT
				//skip forward to the first 0xFF not followed by 0x00 or 0xD0-0xD8.
				while (off < dtend) 
				{
					if (data[off] == 0xff) {
						if (data[off + 1] != 0x00) {
							if (data[off + 1] < 0xD0 || data[off + 1] > 0xD8) {
								break;
							}
						}
					}
					off++; 
				}
				continue;
			}
			if (c2 >= 0xc0 && c2 <= 0xc3) {

				// It's the size frame we read his dimensions, print them and stop
				off += 3;
				unsigned char be = data[off]; off++;
				unsigned char le = data[off]; off++;
				int H = 256 * be + le;
				be = data[off]; off++;
				le = data[off]; off++;
				int W = 256 * be + le;
				//printf("\t<-- +++ HxW : %d x %d\n", H, W);
				
			}
			else {
				//It's not the size bloc, we just read his size to skip it 
				unsigned char be = data[off]; off++;
				unsigned char le = data[off]; off++;
				int OFS = 256 * be + le;
				// printf ("\t OFFSET = %d",OFS);
				off += OFS - 2;
			}
			
		
		}
	}
}
int GetJPEGByteSize_TRUE(unsigned char* data, int dtend)
{
	int off = 2;
	while (off < dtend) {
		unsigned char c1 = data[off]; off++;
		if (c1 == 0xff ) {
			unsigned char c2 = data[off]; off++;
			if (c2 >= 0xc0 && c2 <= 0xc3) {

				// It's the size frame we read his dimensions, print them and stop
				off += 3;
				unsigned char be = data[off]; off++;
				unsigned char le = data[off]; off++;
				int H = 256 * be + le;
				be = data[off]; off++;
				le = data[off]; off++;
				int W = 256 * be + le;
				//printf("\t<-- +++ HxW : %d x %d\n", H, W);
				//std::cout << "current offset " << off << std::endl;
				
				
			}
			else if (c2 == 0xD9) {
				//std::cout << "end was found" << std::endl;
				return off;
			}
			else {
				//It's not the size bloc, we just read his size to skip it 
				unsigned char be = data[off]; off++;
				unsigned char le = data[off]; off++;
				int OFS = 256 * be + le;
				// printf ("\t OFFSET = %d",OFS);
				off += OFS - 2;
			}
		}
	}
	return 0;

}

void ReadJPEGProps(unsigned char * data) {          // data is an array of bytes
	int off = 0; 
	while (off < 512) // bytes sector 
	{
		while (data[off] == 0xff) off++; 
		unsigned char  mrkr = data[off]; off++; 
		if (mrkr == 0xd8) continue;    // SOI
		if (mrkr == 0xd9) break;       // EOI // 
		if (0xd0 <= mrkr && mrkr <= 0xd7) continue;
		if (mrkr == 0x01) continue;    // TEM
		unsigned char  len = (data[off] << 8) | data[off + 1];  off += 2;
		if (mrkr == 0xc0) {
			byte bpc = data[off]; // precission (bits per channel)
			byte h = (data[off + 1] << 8) | data[off + 2];
			byte w = (data[off + 3] << 8) | data[off + 4];
		}
		off += len - 2;
	}
}

bool isJPEGMAGICNUMBER_B(unsigned char * data) {
	//FF D8 FF E0 : JPG

	if ( data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF && data[3] == 0xE0) {
		return true;
	}
	return false;
}
bool isJPEGMAGICNUMBER_L(unsigned char* data) {
	//FF D8 FF E0 : JPG

	if (data[3] == 0xFF && data[2] == 0xD8 && data[1] == 0xFF && data[0] == 0xE0) {
		return true;
	}
	return false;
}

int PrintRootDirInfo(unsigned char* data)
{

	std::cout << "________________ ROOT DIR ENTRY DATA ________________" << std::endl;
	switch (data[0]) {
	case 0x00:
		std::cout << "Free entry. [END OF DIRECTORY]" << std::endl;
		return -1;
		break;
	case 0xE5:
		std::cout << "Deleted entry" << std::endl;
		return -2;
		break;
	}
	// flag 

	unsigned char _n[8];
	memcpy(_n, data, 8);

	unsigned char _ext[3];
	memcpy(_ext, data + 0x08, 3);
	unsigned short first_cluster = BytesToShort(data + 0x1A);
	unsigned int file_size = BytesToUint(data + 0x1C);
	std::cout << "Name : ";
	for (int i = 0; i < 8; i++) {
		printf("%c", _n[i]);
	}
	std::cout << std::endl;
	std::cout << "Ext : ";
	for (int i = 0; i < 3; i++) {
		printf("%c", _ext[i]);
	}
	std::cout << std::endl;
	std::cout << "First cluster index : " << first_cluster << std::endl;
	std::cout << "Byte size : " << file_size << std::endl;
	switch (data[11]) {
	case 0x01:
		std::cout << "READ ONLY" << std::endl;
		break;
	case 0x02:
		std::cout << "HIDDEN FILE" << std::endl;
		break;
	case 0x04:
		std::cout << "SYS FILE" << std::endl;
		break;
	case 0x08:
		std::cout << "VOLUME LABEL" << std::endl;
		break;
	case 0x0f:
		std::cout << "IS LONG FILE NAME" << std::endl;
		break;
	case 0x10:
		std::cout << "IS DIR" << std::endl;
		if (_n[0] != '.') {
			return (int)first_cluster;
		}
		break;
	case 0x20:
		std::cout << "IS ARCHIVE" << std::endl;
		return -3;
		break;
	case 0x40:
		std::cout << "DEVICE" << std::endl;

		break;
	case 0x80:
		std::cout << "NOT USED" << std::endl;
		break;
	}
	return -2;
}

void PrintDirEntry(DIR_ENTRY* d)
{
	std::cout << "________________ DIR ENTRY DATA ________________" << std::endl;
	switch (d->_mark) {
	case 0x00:
		std::cout << "Free entry. [END OF DIRECTORY]" << std::endl;
		break;
	case 0xE5:
		std::cout << "Deleted entry" << std::endl;
		break;
	}
	std::cout << "name : ";
	for (int i = 0; i < 8; i++) {
		printf("%c", d->_name[i]);
	}
	std::cout << std::endl;
	std::cout << "ext : ";
	for (int i = 0; i < 3; i++) {
		printf("%c", d->_ext[i]);
	}
	std::cout << std::endl;
	std::cout << "first cluster index : " << d->_clusterindex << std::endl;
	std::cout << "byte size : " << d->_filesize << std::endl;
	switch (d->_flag) {
	case 0x01:
		std::cout << "READ ONLY" << std::endl;
		break;
	case 0x02:
		std::cout << "HIDDEN FILE" << std::endl;
		break;
	case 0x04:
		std::cout << "SYS FILE" << std::endl;
		break;
	case 0x08:
		std::cout << "VOLUME LABEL" << std::endl;
		break;
	case 0x0f:
		std::cout << "IS LONG FILE NAME" << std::endl;
		break;
	case 0x10:
		std::cout << "IS DIR" << std::endl;
		break;
	case 0x20:
		std::cout << "IS ARCHIVE" << std::endl;
		break;
	case 0x40:
		std::cout << "DEVICE" << std::endl;

		break;
	case 0x80:
		std::cout << "NOT USED" << std::endl;
		break;
	}
}

// misc
uint32_t BytesToUint(unsigned char* arr)
{
	uint32_t foo;
	// big endian
	foo = (uint32_t)arr[3] << 24;
	foo |= (uint32_t)arr[2] << 16;
	foo |= (uint32_t)arr[1] << 8;
	foo |= (uint32_t)arr[0];

	return foo;
}

void UintToBytes(uint32_t v, unsigned char* a)
{
	a[3] = v >> 24;
	a[2] = v >> 16;
	a[1] = v >> 8;
	a[0] = v;
}

uint16_t BytesToShort(unsigned char* arr)
{
	return  ((arr[1] << 8) | arr[0]);
}
