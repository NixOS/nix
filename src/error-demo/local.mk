programs += error-demo

error-demo_DIR := $(d)

error-demo_SOURCES := \
  $(wildcard $(d)/*.cc) \

error-demo_CXXFLAGS += -I src/libutil -I src/libexpr

error-demo_LIBS = libutil libexpr

error-demo_LDFLAGS = -pthread $(SODIUM_LIBS) $(EDITLINE_LIBS) $(BOOST_LDFLAGS) -lboost_context -lboost_thread -lboost_system
