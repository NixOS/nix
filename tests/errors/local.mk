programs += error-test

error-test_DIR := $(d)

error-test_SOURCES := \
  $(wildcard $(d)/*.cc) \

error-test_LIBS = libutil

error-test_LDFLAGS = -pthread $(SODIUM_LIBS) $(EDITLINE_LIBS) $(BOOST_LDFLAGS) -lboost_context -lboost_thread -lboost_system
