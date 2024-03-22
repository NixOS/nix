#if __linux__
void chrootSetup(Path &chrootRootDir);
void cgroupSetup();
void setupChrootNamespaces(bool &setUser);
void createChild(const std::string &slaveName);
void materialiseRecursiveDependency(const StorePath &path);
#endif
