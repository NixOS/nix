#include "derivations.hh"
#include "globals.hh"
#include "shared.hh"
#include "store-api.hh"
#include <sys/utsname.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <libgen.h>
#include <mach-o/loader.h>
#include <mach-o/swap.h>
#include <yaml-cpp/yaml.h>

using namespace nix;

typedef std::map<string, std::set<string>> SetMap;

static auto cacheDir = Path{};

Path resolveCacheFile(const Path & lib) {
    Path lib2 = Path(lib);
    std::replace(lib2.begin(), lib2.end(), '/', '%');
    return cacheDir + "/" + lib2;
}

std::set<string> readCacheFile(const Path & file) {
    return tokenizeString<set<string>>(readFile(file), "\n");
}

void writeCacheFile(const Path & file, std::set<string> & deps) {
    std::ofstream fp;
    fp.open(file);
    for (auto & d : deps) {
        fp << d << "\n";
    }
    fp.close();
}

std::string find_dylib_name(FILE *obj_file, struct load_command cmd) {
  fpos_t pos;
  fgetpos(obj_file, &pos);
  struct dylib_command dylc;
  dylc.cmd = cmd.cmd;
  dylc.cmdsize = cmd.cmdsize;
  fread(&dylc.dylib, sizeof(struct dylib), 1, obj_file);

  char *dylib_name = (char*)calloc(cmd.cmdsize, sizeof(char));
  fseek(obj_file,
      // offset is calculated from the beginning of the load command, which is two
      // uint32_t's backwards
      dylc.dylib.name.offset - (sizeof(uint32_t) * 2) + pos,
      SEEK_SET);
  fread(dylib_name, sizeof(char), cmd.cmdsize, obj_file);
  fseek(obj_file, pos, SEEK_SET);
  return std::string(dylib_name);
}

bool seek_mach64_blob(FILE *obj_file, enum NXByteOrder end) {
  struct fat_header head;
  fread(&head, sizeof(struct fat_header), 1, obj_file);
  swap_fat_header(&head, end);
  for(uint32_t narches = 0; narches < head.nfat_arch; narches++) {
    struct fat_arch arch;
    fread(&arch, sizeof(struct fat_arch), 1, obj_file);
    swap_fat_arch(&arch, 1, end);
    if(arch.cputype == CPU_TYPE_X86_64) {
      fseek(obj_file, arch.offset, SEEK_SET);
      return true;
    }
  }
  return false;
}

std::set<std::string> runResolver(const Path & filename) {
  FILE *obj_file = fopen(filename.c_str(), "rb");
  uint32_t magic;
  fread(&magic, sizeof(uint32_t), 1, obj_file);
  fseek(obj_file, 0, SEEK_SET);
  enum NXByteOrder endianness;
  if(magic == 0xBEBAFECA) {
    endianness = NX_BigEndian;
    if(!seek_mach64_blob(obj_file, endianness)) {
      std::cerr << "Could not find any mach64 blobs in file " << filename << ", continuing..." << std::endl;
      return std::set<string>();
    }
  }
  struct mach_header_64 header;
  fread(&header, sizeof(struct mach_header_64), 1, obj_file);
  if(!(header.magic == MH_MAGIC_64 || header.magic == MH_CIGAM_64)) {
    std::cerr << "Not a mach-o object file: " << filename << std::endl;
    return std::set<string>();
  }
  std::set<string> libs;
  for(uint32_t i = 0; i < header.ncmds; i++) {
    struct load_command cmd;
    fread(&cmd.cmd, sizeof(uint32_t), 1, obj_file);
    fread(&cmd.cmdsize, sizeof(uint32_t), 1, obj_file);
    switch(cmd.cmd) {
      case LC_LOAD_DYLIB:
      case LC_REEXPORT_DYLIB:
        libs.insert(find_dylib_name(obj_file, cmd));
        break;
    }
    fseek(obj_file, cmd.cmdsize - (sizeof(uint32_t) * 2), SEEK_CUR);
  }
  fclose(obj_file);
  libs.erase(filename);
  return libs;
}

bool isSymlink(const Path & path) {
  struct stat st;
  if(lstat(path.c_str(), &st))
      throw SysError(format("getting attributes of path ‘%1%’") % path);

  return S_ISLNK(st.st_mode);
}

Path resolveSymlink(const Path & path) {
    char buf[PATH_MAX];
    ssize_t len = readlink(path.c_str(), buf, sizeof(buf) - 1);
    if(len != -1) {
      buf[len] = 0;
      if(buf[0] == '/') {
        return Path(buf);
      } else {
        return Path(dirname(strdup(path.c_str()))) + "/" + Path(buf);
      }
    } else {
      throw SysError(format("readlink('%1%')") % path);
    }
}

bool isTbdFile(const Path & path) {
    std::string ending = ".tbd";
    return 0 == path.compare(path.length() - ending.length(), ending.length(), ending);
}

std::string resolveTbdTarget(const Path & path) {
    YAML::Node tbd = YAML::LoadFile(path);
    return tbd["install-name"].as<std::string>();
}

std::set<string> resolve_tree(const Path & path, PathSet & deps) {
  std::set<string> results;
  if(deps.find(path) != deps.end()) {
    return std::set<string>();
  }
  deps.insert(path);
  for (auto & lib : runResolver(path)) {
    results.insert(lib);
    for (auto & p : resolve_tree(lib, deps)) {
      results.insert(p);
    }
  }
  return results;
}

std::set<string> get_path(const Path & path) {
  Path cacheFile = resolveCacheFile(path);
  if(pathExists(cacheFile)) {
    return readCacheFile(cacheFile);
  }

  std::set<string> deps;
  std::set<string> paths;
  paths.insert(path);

  Path next_path = Path(path);
  while(isSymlink(next_path)) {
    next_path = resolveSymlink(next_path);
    paths.insert(next_path);
  }

  if(isTbdFile(next_path)) {
    next_path = resolveTbdTarget(next_path);
    paths.insert(next_path);
  }

  for(auto & t : resolve_tree(next_path, deps)) {
    paths.insert(t);
  }

  writeCacheFile(cacheFile, paths);

  return paths;
}

int main(int argc, char ** argv) {
    return handleExceptions(argv[0], [&]() {
        initNix();

        struct utsname _uname;

        uname(&_uname);

        cacheDir = (format("%1%/dependency-maps/%2%-%3%-%4%")
            % settings.nixStateDir
            % _uname.machine
            % _uname.sysname
            % _uname.release).str();

        mkdir(cacheDir.c_str(), 0644);

        auto store = openStore();

        auto drv = store->derivationFromPath(Path(argv[1]));
        Strings impurePaths = tokenizeString<Strings>(get(drv.env, "__impureHostDeps"));

        std::set<string> all_paths;

        for (auto & path : impurePaths) {
            for(auto & p : get_path(path)) {
              all_paths.insert(p);
            }
        }

        std::cout << "extra-chroot-dirs" << std::endl;
        for(auto & path : all_paths) {
          std::cout << path << std::endl;
        }
        std::cout << std::endl;
    });
}
