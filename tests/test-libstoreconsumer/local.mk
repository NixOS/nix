programs += test-libstoreconsumer

test-libstoreconsumer_DIR := $(d)

# do not install
test-libstoreconsumer_INSTALL_DIR :=

test-libstoreconsumer_SOURCES := \
  $(wildcard $(d)/*.cc) \

test-libstoreconsumer_CXXFLAGS += -I src/libutil -I src/libstore

test-libstoreconsumer_LIBS = libstore libutil

test-libstoreconsumer_LDFLAGS = -pthread $(SODIUM_LIBS) $(EDITLINE_LIBS) $(BOOST_LDFLAGS) $(LOWDOWN_LIBS)
