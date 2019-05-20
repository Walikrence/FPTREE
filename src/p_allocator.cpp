#include"utility/p_allocator.h"
#include<iostream>
#include<string.h>
#include<assert.h>
using namespace std;

// the file that store the information of allocator
const string P_ALLOCATOR_CATALOG_NAME = "p_allocator_catalog";
// a list storing the free leaves
const string P_ALLOCATOR_FREE_LIST    = "free_list";

PAllocator* PAllocator::pAllocator = new PAllocator();

PAllocator* PAllocator::getAllocator() {
    if (PAllocator::pAllocator == NULL) {
        PAllocator::pAllocator = new PAllocator();
    }
    return PAllocator::pAllocator;
}

/* data storing structure of allocator
   In the catalog file, the data structure is listed below
   | maxFileId(8 bytes) | freeNum = m | treeStartLeaf(the PPointer) |
   In freeList file:
   | freeList{(fId, offset)1,...(fId, offset)m} |
*/
PAllocator::PAllocator() {
    string allocatorCatalogPath = DATA_DIR + P_ALLOCATOR_CATALOG_NAME;
    string freeListPath         = DATA_DIR + P_ALLOCATOR_FREE_LIST;
    ifstream allocatorCatalog(allocatorCatalogPath, ios::in|ios::binary);
    ifstream freeListFile(freeListPath, ios::in|ios::binary);
    // judge if the catalog exists
    if (allocatorCatalog.is_open() && freeListFile.is_open()) {
        // exist
        // TODO
        char* num;
        allocatorCatalog.read(num,sizeof(uint64_t));
        this->maxFileId = uint64_t(num);
        allocatorCatalog.read(num,sizeof(uint64_t));
        this->freeNum = uint64_t(num);
        allocatorCatalog.read(num,sizeof(PPointer));
        this->PPointer = PPointer(num);

        PPointer flp;
        for(int i = 0;i < this->freeNum ; i++) {
            freeListFile.read((char*)&(flp),sizeof(PPointer));
            this->freeList.push_back(flp);
        }
        allocatorCatalog.close();
        freeList.close();
    } else {
        // not exist, create catalog and free_list file, then open.
        // TODO
        fstream file1;
        file1.open(allocatorCatalogPath,ios::out);
        if(!file1) {return false;}
        maxFileId = 1;
        freeNum = 0;
        this->startLeaf.fileId = 0;
        this->startLeaf.offset = LEAF_GROUP_HEAD;
        file1.write((char*)&(maxFileId),sizeof(uint64_t));
        file1.write((char*)&(freeNum),sizeof(uint64_t));
        file1.write((char*)&(startLeaf),sizeof(PPointer));
        file1.close();
        fstream file2;
        file2.open(freeListPath,ios::out);
        if(!file2) {return false;}
        file2.close();
    }
    this->initFilePmemAddr();
}

PAllocator::~PAllocator() {
    // TODO
    auto it = fId2PmAddr.begin();
    while(it != fId2PmAddr.end()){
        int leaf_len = 8 + LEAF_GROUP_AMOUNT + LEAF_GROUP_AMOUNT * calLeafSize();
        pmem_msync(it->second,leaf_len);
        pmem_unmap(it->second,leaf_len);
        it++;        
    }
}

// memory map all leaves to pmem address, storing them in the fId2PmAddr
void PAllocator::initFilePmemAddr() {
    // TODO
    int is_pmem;
    size_t mapped_len;
    int leaf_len = 8 +  LEAF_GROUP_AMOUNT + LEAF_GROUP_AMOUNT*calLeafSize();
    for (uint64_t i = 0;i < maxFileId;i++) {
        char * pmemaddr;
        string file_path = DATA_DIR + to_string(i);
        if ((pmemaddr = (char*)pmem_map_file(filePath.c_str(), leaf_group_len, PMEM_FILE_CREATE,
            0666, &mapped_len, &is_pmem)) == NULL) {
            assert(pmemaddr != NULL);
        }
        fId2PmAddr.insert(pair<uint64_t,char*>(i,pmemaddr));
    }
}

// get the pmem address of the target PPointer from the map fId2PmAddr
char* PAllocator::getLeafPmemAddr(PPointer p) {
    // TODO
    map<uint64_t,char*>::iterator iter = fId2PmAddr.find(p.fileId);
    if(iter = fId2PmAddr.end()) {return NULL;}
    char* addr = iter->second;
    if(p.offset > LEAF_GROUP_HEAD + (LEAF_GROUP_AMOUNT - 1)*calLeafSize) return NULL;
    return addr + p.offset;
}

// get and use a leaf for the fptree leaf allocation
// return 
bool PAllocator::getLeaf(PPointer &p, char* &pmem_addr) {
    // TODO
    if(freeList.empty()) {
        bool em = newLeafGroup();
        if(!em) return false;
    }
    p.fileId = freeList[freeList.size()-1].fileId;
    p.offset = freeList[freeList.size()-1].offset;
    if((pmem_addr = getLeafPmemAddr(p)) == NULL) return false;
    auto it = freeList.end();
    --it;
    freeList.erase(it);
    map<uint64_t,char*>::iterator iter = fId2PmAddr.find(p.fileId);
    char* addre = iter->second;
    uint64_t usdnum;
    memcpy(&usdnum,addre,sizeof(uint64_t));
    usdnum++;
    memcpy(addre,&usdnum,sizeof(uint64_t));
    uint64_t weizhi = (p.offset - LEAF_GROUP_HEAD)/calLeafSize();
    addre = addre + 8 + weizhi;
    *addre = 1;
    addre = addre - 8 - weizhi;
    int leaf_len = LEAF_GROUP_HEAD + LEAF_GROUP_AMOUNT * calLeafSize();
    pmem_msync(addre,leaf_len);
    freeNum--;
    return true;

}

bool PAllocator::ifLeafUsed(PPointer p) {
    // TODO
    if(p.fileId >= maxFileId) return false;
    if(p.offset > LEAF_GROUP_HEAD + (LEAF_GROUP_AMOUNT - 1)*calLeafSize()) return false;
    if(freeList.find(p.fileId) != freeList.end()) return false;
    return true;
}

bool PAllocator::ifLeafFree(PPointer p) {
    // TODO
    if(!ifLeafExist(p)) return false;
    if(ifLeafUsed(p)) return false;
    return true;
}

// judge whether the leaf with specific PPointer exists. 
bool PAllocator::ifLeafExist(PPointer p) {
    // TODO
    if(p.fileId >= maxFileId) return false;
    if(p.offset > LEAF_GROUP_HEAD + (LEAF_GROUP_AMOUNT - 1)*calLeafSize()) return false;
    return true;
}

// free and reuse a leaf
bool PAllocator::freeLeaf(PPointer p) {
    // TODO
    if(!ifLeafExist(p)) return false;
    if(ifLeafFree(p)) return true;

    map<uint64_t,char*>::iterator iter = fId2PmAddr.find(p.fileId);
    char* addre = iter->second;
    uint64_t usdnum;
    memcpy(&usdnum,addre,sizeof(uint64_t));
    usdnum--;
    memcpy(addre,&usdnum,sizeof(uint64_t));
    int locan = (p.offset - LEAF_GROUP_HEAD)/calLeafSize();
    addre = addre + 8 + locan;
    *addre = 0;
    addre = addre - 8 - locan;
    int leaf_len = LEAF_GROUP_HEAD + LEAF_GROUP_AMOUNT * calLeafSize();
    pmem_msync(addre, leaf_len);
    freeList.push_back(p);
    freeNum++;
    return true;
}

bool PAllocator::persistCatalog() {
    // TODO
    string allcat_path = DATA_DIR + P_ALLOCATOR_CATALOG_NAME;
    string frefile_path = DATA_DIR + P_ALLOCATOR_FREE_LIST;
    ofstream allocat(allcat_path,ios::out);
    ofstream freelist_file(frefile_path,ios::path);
    if (!(allocat.is_open && freelist_file.is_open()))
    {
        /* code */
        return false;
    }

    allocat.write((char*)&(this->maxFileId),sizeof(uint64_t));
    allocat.write((char*)&(this->freeNum),sizeof(uint64_t));
    allocat.write((char*)&(this->startLeaf),sizeof(PPointer));

    for (uint64_t i = 0; i < freeNum; ++i)
    {
        /* code */
        freelist_file.write((char*)&(freeList[i]),sizeof(PPointer));
    }
  
    return true;
}

/*
  Leaf group structure: (uncompressed)
  | usedNum(8b) | bitmap(n * byte) | leaf1 |...| leafn |
*/
// create a new leafgroup, one file per leafgroup
bool PAllocator::newLeafGroup() {
    // TODO
    char *pmemaddr;
    int is_pmem;
    size_t mapped_len;
    int leaf_group_len = 8 + LEAF_GROUP_AMOUNT + LEAF_GROUP_AMOUNT*calLeafSize();
    if ((pmemaddr = (char*)pmem_map_file(newFilePath.c_str(), leaf_group_len, PMEM_FILE_CREATE,
        0666, &mapped_len, &is_pmem)) == NULL){
            assert(pmemaddr!=NULL);
            }
    fId2PmAddr.insert(pair<uint64_t,char*>(maxFileId,pmemaddr));
    for(uint64_t i = 0,i < LEAF_GROUP_AMOUNT,i++) {
        PPointer num0;
        num0.fileId = maxFileId;
        num0.offset = LEAF_GROUP_HEAD + i * calLeafSize();
        freeList.push_back(num0);
        this->freeNum++;
    }
    this->maxFileId++;
    if(maxFileId == 2) {
        startLeaf.fileId = 1;
        startLeaf.offset = LEAF_GROUP_HEAD+(LEAF_GROUP_AMOUNT-1)*calLeafSize();
    }

    string new_path = DATA_DIR + to_string(maxFileId-1);
    ofstream new_file_c(new_path, ios::out);
    uint64_t usdnum = 0;
    new_file_c.write((char*)&(usdnum),sizeof(uint64_t));
    char* bitmap = "0000000000000000";
    new_file_c.write(bitmap,16);
    char leaf_len[LEAF_GROUP_AMOUNT*calLeafSize()];
    memset(leaf_len,0,sizeof(leaf_len));
    new_file_c.write((char*)&(leaf_len),sizeof(leaf_len));

    return false;
}