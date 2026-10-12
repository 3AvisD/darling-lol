// Offline AIR translation and SPIR-V export; no game/client process is involved.
#include <iridium/iridium.hpp>
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr,"usage: %s <metallib-directory> <output-directory> [limit]\n",argv[0]);return 2; }
    if (!Iridium::init()) { fprintf(stderr,"Cannot initialize LLVM\n");return 2; }
    DIR* directory = opendir(argv[1]); if(!directory)return 2;
    std::vector<std::string> names;
    while(auto entry=readdir(directory)){std::string name=entry->d_name;if(name.size()>4 && name.substr(name.size()-4)==".bin")names.push_back(name);}
    closedir(directory);std::sort(names.begin(),names.end());
    if(argc>3 && names.size()>strtoul(argv[3],nullptr,10)) names.resize(strtoul(argv[3],nullptr,10));
    unsigned passed=0,failed=0;std::map<std::string,unsigned> reasons;
    for(const auto& name:names){
        fprintf(stderr,"CHECK: %s\n",name.c_str());
        std::ifstream file(std::string(argv[1])+"/"+name,std::ios::binary);
        std::vector<char> input((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        try {
            size_t size=0;Iridium::OutputInfo info;
            void* output=Iridium::translate(input.data(),input.size(),size,info);
            if(!output || !size)throw std::runtime_error("Empty translated module");
            // An output directory of "-" only checks that translation succeeds
            if(std::string(argv[2])=="-"){free(output);++passed;continue;}
            std::string path=std::string(argv[2])+"/"+name+".spv";
            FILE* target=fopen(path.c_str(),"wb");
            if(!target){free(output);throw std::runtime_error("Cannot open output file");}
            bool written=fwrite(output,1,size,target)==size;fclose(target);free(output);
            if(!written)throw std::runtime_error("Cannot write translated module");
            ++passed;
        }catch(const std::exception& error){++failed;++reasons[error.what()];fprintf(stderr,"FAIL: %s: %s\n",name.c_str(),error.what());}
    }
    printf("Translation: %u passed, %u failed\n",passed,failed);
    for(const auto& reason:reasons)printf("%u failures: %s\n",reason.second,reason.first.c_str());
    Iridium::finit();return failed?1:0;
}
