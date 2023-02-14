#pragma once
#include <random>
#include <vector>
#include <cassert>

#include "lib.hh" 
#include "rolex_util_back.hh" 
#include "load_config.hh"  


namespace rolex{ 

using namespace bench;
using namespace xstore;


std::vector<u64> exist_keys;
std::vector<u64> nonexist_keys;

// ========= functions ==============
void load_data();
void normal_data();
void lognormal_data();
void load_ycsb(const char *path);
void run_ycsb(const char *path);
void weblog_data();
void documentid_data();


typedef struct operation_item {
    KeyType key;
    int32_t range;
    uint8_t op;
} operation_item;

struct {
    size_t operate_num = 10000000;
    operation_item* operate_queue;
}YCSBconfig;



void load_data() {
	switch(BenConfig.workloads) {
		case NORMAL:
			normal_data();
      LOG(3) << "==== LOAD normal =====";
			break;
    case LOGNORMAL:
			lognormal_data();
      LOG(3) << "==== LOAD lognormal =====";
			break;
    case WEBLOG:
			weblog_data();
      LOG(3) << "==== LOAD weblog =====";
			break;
    case DOCID:
			documentid_data();
      LOG(3) << "==== LOAD documentID =====";
			break;
    case YCSB_A:
      load_ycsb("/data/lpf/data/ycsb/uniform/workloada/load.10M");
      run_ycsb("/data/lpf/data/ycsb/uniform/workloada/run.10M");
      LOG(3) << "==== LOAD YCSB workload A =====";
      break;
    case YCSB_B:
      load_ycsb("/data/lpf/data/ycsb/uniform/workloadb/load.10M");
      run_ycsb("/data/lpf/data/ycsb/uniform/workloadb/run.10M");
      LOG(3) << "==== LOAD YCSB workload B =====";
      break;
    case YCSB_C:
      load_ycsb("/data/lpf/data/ycsb/uniform/workloadc/load.10M");
      run_ycsb("/data/lpf/data/ycsb/uniform/workloadc/run.10M");
      LOG(3) << "==== LOAD YCSB workload C =====";
      break;
    case YCSB_D:
      load_ycsb("/data/lpf/data/ycsb/uniform/workloadd/load.10M");
      run_ycsb("/data/lpf/data/ycsb/uniform/workloadd/run.10M");
      LOG(3) << "==== LOAD YCSB workload D =====";
      break;
    case YCSB_E:
      load_ycsb("/data/lpf/data/ycsb/uniform/workloade/load.10M");
      run_ycsb("/data/lpf/data/ycsb/uniform/workloade/run.10M");
      LOG(3) << "==== LOAD YCSB workload E =====";
      break;
    case YCSB_F:
      YCSBconfig.operate_num = 15000749;   // Uniform
      //YCSBconfig.operate_num = 14999193;     // Zipfian
      load_ycsb("/data/lpf/data/ycsb/uniform/workloadf/load.10M");
      run_ycsb("/data/lpf/data/ycsb/uniform/workloadf/run.10M");
      LOG(3) << "==== LOAD YCSB workload F =====";
      break;
		default:
			LOG(4) << "WRONG benchmark " << BenConfig.workloads;
			exit(0);
	}

  LOG(3) << "Exist keys: " << exist_keys.size();
  LOG(3) << "nonExist keys: " << nonexist_keys.size();
}

/**
 * @brief generate normal data
 * 		benchmark 0: normal data
 */
void normal_data_back() {
  exist_keys.reserve(BenConfig.nkeys);
  for(size_t i=0; i<BenConfig.nkeys; i++) {
    u64 a = BenConfig.non_nkeys+i;
    exist_keys.push_back(a);
  }
  nonexist_keys.reserve(BenConfig.non_nkeys);
  for(size_t i=0; i<BenConfig.non_nkeys; i++) {
    u64 a = i;
    nonexist_keys.push_back(a);
  }
}

void normal_data() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::normal_distribution<double> rand_normal(4, 2);
  
  exist_keys.reserve(BenConfig.nkeys);
  for(size_t i=0; i<BenConfig.nkeys; i++) {
    u64 a = rand_normal(gen)*1000000000000;
    if(a<0) {
      i--;
      continue;
    }
    exist_keys.push_back(a);
  }
  nonexist_keys.reserve(BenConfig.non_nkeys);
  for(size_t i=0; i<BenConfig.non_nkeys; i++) {
    u64 a = rand_normal(gen)*1000000000000;
    if(a<0) {
      i--;
      continue;
    }
    nonexist_keys.push_back(a);
  }
}

void lognormal_data(){
    std::random_device rd;
    std::mt19937 gen(rd());
    std::lognormal_distribution<double> rand_lognormal(0, 2);

    exist_keys.reserve(BenConfig.nkeys);
    for (size_t i = 0; i < BenConfig.nkeys; ++i) {
        u64 a = rand_lognormal(gen)*1000000000000;
        assert(a>0);
        exist_keys.push_back(a);
    }
    if (BenConfig.insert_ratio > 0) {
        nonexist_keys.reserve(BenConfig.non_nkeys);
        for (size_t i = 0; i < BenConfig.non_nkeys; ++i) {
            u64 a = rand_lognormal(gen)*1000000000000;
            assert(a>0);
            nonexist_keys.push_back(a);
        }
    }
}

void load_ycsb(const char *path) {
  FILE *ycsb, *ycsb_read;
	char *buf = NULL;
	size_t len = 0;
  size_t item_num = 0;
  char key[16];
  KeyType dummy_key = 1234;
    
    
  ASSERT((ycsb = fopen(path,"r")) != NULL) << "Fail to open YCSB data!";
  
  LOG(3)<< "======== load YCSB data =========";
  int n = 10000000;
  exist_keys.reserve(n);
	while(getline(&buf,&len,ycsb) != -1){
	  if(strncmp(buf, "INSERT", 6) == 0){
      memcpy(key, buf+7, 16);
      dummy_key = strtoul(key, NULL, 10);
      exist_keys.push_back(dummy_key);
	  }
    item_num++;
	}
	fclose(ycsb);
  assert(exist_keys.size()==item_num);
  LOG(3)<<"load number : " << item_num;
}




void run_ycsb(const char *path){
  FILE *ycsb, *ycsb_read;
	char *buf = NULL;
	size_t len = 0;
  size_t query_i = 0, insert_i = 0, delete_i = 0, update_i = 0, scan_i = 0;
  size_t item_num = 0;
  char key[16];
  KeyType dummy_key = 1234;
    

  ASSERT((ycsb = fopen(path,"r")) != NULL) << "Fail to open YCSB data!";
  YCSBconfig.operate_queue = (operation_item *)malloc(YCSBconfig.operate_num*sizeof(operation_item));

	while(getline(&buf,&len,ycsb) != -1){
		if(strncmp(buf, "READ", 4) == 0){
      memcpy(key, buf+5, 16);
      dummy_key = strtoul(key, NULL, 10);
      YCSBconfig.operate_queue[item_num].key = dummy_key;
      YCSBconfig.operate_queue[item_num].op = 0;
      item_num++;
      query_i++; 
		} else if(strncmp(buf, "INSERT", 6) == 0) {
      memcpy(key, buf+7, 16);
      dummy_key = strtoul(key, NULL, 10);
      YCSBconfig.operate_queue[item_num].key = dummy_key;
      YCSBconfig.operate_queue[item_num].op = 1;
      item_num++;
      insert_i++;
    } else if(strncmp(buf, "UPDATE", 6) == 0) {
      memcpy(key, buf+7, 16);
      dummy_key = strtoul(key, NULL, 10);
      YCSBconfig.operate_queue[item_num].key = dummy_key;
      YCSBconfig.operate_queue[item_num].op = 2;
      item_num++;
      update_i++;
    } else if(strncmp(buf, "REMOVE", 6) == 0) {
      memcpy(key, buf+7, 16);
      dummy_key = strtoul(key, NULL, 10);
      YCSBconfig.operate_queue[item_num].key = dummy_key;
      YCSBconfig.operate_queue[item_num].op = 3;
      item_num++;
      delete_i++;
    } else if(strncmp(buf, "SCAN", 4) == 0) {
      int range_start= 6;
      while(strncmp(&buf[range_start], " ", 1) != 0)
          range_start++;
      memcpy(key, buf+5, range_start-5);
      dummy_key = strtoul(key, NULL, 10);
      range_start++;
      int range = atoi(&buf[range_start]);

      YCSBconfig.operate_queue[item_num].key = dummy_key;
      YCSBconfig.operate_queue[item_num].range = range;
      YCSBconfig.operate_queue[item_num].op = 4;
      item_num++;
      scan_i++;
    } else {
      continue;
    }
	}
	fclose(ycsb);
  LOG(3)<<"  read: " << query_i;
  LOG(3)<<"insert: " << insert_i;
  LOG(3)<<"update: " << update_i;
  LOG(3)<<"remove: " << delete_i;
  LOG(3)<<"  scan: " << scan_i;
  ASSERT(item_num == YCSBconfig.operate_num) << "LOAD YCSB error";
}


#define BUF_SIZE 2048

std::vector<int64_t> read_timestamp(const char *path) {
    std::vector<int64_t> vec;
    FILE *fin = fopen(path, "rb");
    int64_t buf[BUF_SIZE];
    while (true) {
        size_t num_read = fread(buf, sizeof(int64_t), BUF_SIZE, fin);
        for (size_t i = 0; i < num_read; i++) {
            vec.push_back(buf[i]);
        }
        if (num_read < BUF_SIZE) break;
    }
    fclose(fin);
    return vec;
}


void weblog_data(){
    //assert(Config.read_ratio == 1);
    std::vector<int64_t> read_keys = read_timestamp("/data/lpf/data/timestamp.sorted.200M");
    size_t size = read_keys.size();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> rand_int32(
        0, size);

    if(BenConfig.insert_ratio > 0) {
        size_t train_size = size/10;
        exist_keys.reserve(train_size);
        for(size_t i=0; i<train_size; i++){
            exist_keys.push_back(read_keys[rand_int32(gen)]);
        }

        nonexist_keys.reserve(size);
        for(size_t i=0; i<size; i++){
            nonexist_keys.push_back(read_keys[i]);
        }
    } else {
        size_t num = BenConfig.nkeys < read_keys.size()? BenConfig.nkeys:read_keys.size();
        exist_keys.reserve(num);
        for(size_t i=0; i<num; i++){
            exist_keys.push_back(read_keys[rand_int32(gen)]);
        }
    }
}

void documentid_data(){
    std::vector<int64_t> read_keys = read_timestamp("/data/lpf/data/document-id.sorted.10M");
    size_t size = read_keys.size();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> rand_int32(
        0, size);

    if(BenConfig.insert_ratio > 0) {
        size_t train_size = size/10;
        exist_keys.reserve(train_size);
        for(size_t i=0; i<train_size; i++){
            exist_keys.push_back(read_keys[rand_int32(gen)]);
        }

        nonexist_keys.reserve(size);
        for(size_t i=0; i<size; i++){
            nonexist_keys.push_back(read_keys[i]);
        }
    } else {
        size_t num = BenConfig.nkeys < read_keys.size()? BenConfig.nkeys:read_keys.size();
        exist_keys.reserve(num);
        for(size_t i=0; i<num; i++){
            exist_keys.push_back(read_keys[rand_int32(gen)]);
        }
    }
}




} // namespace rolex