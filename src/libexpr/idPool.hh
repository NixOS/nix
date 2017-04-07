/* idPool: for each string a unique integer numbers is returned
 * 
 *  motivation:
 *    say you want to connect two inet services, which are using port numbers. this abstraction handles the ID number management.
 *    one can easily write a library function: `lib.getUniquePort "identifier"` and it will return a integer `50000`
 * 
 *  example:
 *    given nix code:                                               after evaluation
 *      port1 = ${lib.getUniquePort "myNginxInstance"}                port1 = 50000
 *      port2 = ${lib.getUniquePort "myNginxInstance"}                port2 = 50000
 *      port3 = ${lib.getUniquePort "bar"}                            port3 = 50001
 * 
 *  http://rapidjson.org/classrapidjson_1_1_generic_value.html#ad290f179591025e871bedbbac89ac276
 */

// nix-instantiate --eval --strict z.nix 

// z.nix
//
// let
//   lib = {
//     port = builtins.uniqueID;
//   };
// in
// {
//   a = { 
//     bar=(lib.port "aa12");
//     foo=(lib.port "aa14"); 
//     foo1=(lib.port "aa14"); 
//     foo12=(lib.port "aa141");
//   };
// }
    
#include <algorithm>
#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <cstdio>
#include <map>
#include <cstring>
#include <regex>

#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "eval.hh"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/prettywriter.h"

// FIXME: check how to get rid of old entries

using namespace std;
using namespace rapidjson;

class idPool {
  public:
    idPool(string fileName) {
      start = 50000;
      range = 1000;
      this->fileName = fileName;
      loadFile();
    };
    ~idPool() {
      saveFile();
    };
    
    int resolve(string serviceName) {
      activeIdentifiers.push_back(serviceName);
      // move serviceName to first element
      int n = 0;
      std::vector<string>::iterator it;
      
      // return associated int
      try {
        n = store.at(serviceName);
      }
      catch (const std::out_of_range& oor) {
        // a new identifier, return an unused number
        it = order.begin();
        n = getFreeID();
        order.insert(it, serviceName);
        store[serviceName]=n;
      }
      return n;
    }  
    
  private:
    std::vector<string> order;
    std::vector<string> activeIdentifiers;
    std::vector<string> inactiveIdentifiers;
    std::map<string, unsigned int> store;
    string fileName;
    unsigned int start;
    unsigned int range;
    
    // FIXME: filter invalid ID mappings (out of range or duplicated use)
    void loadFile() {
      ifstream ifs(fileName.c_str());
      IStreamWrapper isw(ifs);
      
      Document document;
      document.ParseStream(isw);
      
      //static const char* kTypeNames[] = { "Null", "False", "True", "Object", "Array", "String", "Number" };
      //FIXME: check types
      for (Value::ConstMemberIterator itr = document.MemberBegin(); itr != document.MemberEnd(); ++itr) {
        if (itr->value.GetInt() >= start && itr->value.GetInt() < start + range) {
          store[itr->name.GetString()] = itr->value.GetInt();
          std::string a = itr->name.GetString();
          order.push_back(a);
        }
      }
    }
      
    unsigned int getFreeID() {
      std::vector<unsigned int> IDs;
      
      for(std::vector<string>::size_type i = 0; i != order.size(); i++) {
        IDs.push_back(store[order[i]]);
      }
      
      std::sort(IDs.begin(), IDs.end());
      
      for(std::vector<unsigned int>::size_type i = 0; i !=IDs.size(); i++) {
        if (IDs[i] != start + i) 
          return start + i;
      }
      if (IDs.size() <= start + range)
        return start+IDs.size();
      
      throw nix::EvalError("getFreeID() can't find any integer ID left as the complete range in use!");
    }
      
    void saveFile() {
      // activeIdentifiers
      Document document;
      const char json[] = " {  } ";
      document.Parse(json);

      std::cout << std::endl;
      
      for(std::vector<string>::size_type i = 0; i != order.size(); i++) {
        string a = order[i];
        rapidjson::Value strVal;
        strVal.SetString(a.c_str(), a.length(), document.GetAllocator());
        document.AddMember(strVal, store[a], document.GetAllocator());
      }
      
      StringBuffer buffer;
      PrettyWriter<StringBuffer> writer(buffer);
      document.Accept(writer);

      std::ofstream of(fileName.c_str());
      of << buffer.GetString();
      if (!of.good())
        throw nix::EvalError(nix::format("Can't write the idPool JSON data to the file: `%1%`!") % fileName);
    }
};
