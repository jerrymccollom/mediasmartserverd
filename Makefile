SHELL = /bin/bash
CXX ?= g++
CPPFLAGS ?= -Isrc
CXXFLAGS ?= -O2 -g -std=c++17 -Wall -Wextra -Wpedantic
LDLIBS = -ludev
BUILD ?= build
SOURCES = runtime device_monitor update_monitor helper_process event_loop hardware light_show ipc_protocol ipc_server command_dispatch
OBJECTS = $(addprefix $(BUILD)/,$(addsuffix .o,$(SOURCES)))

PREFIX ?= /usr
SBINDIR ?= $(PREFIX)/sbin

.PHONY: all clean install test test-sanitize prepare-for-packaging package-unsigned package-signed
all: mediasmartserverd mediasmartctl

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

mediasmartserverd: $(OBJECTS) $(BUILD)/mediasmartserverd.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

mediasmartctl: $(BUILD)/ipc_protocol.o $(BUILD)/runtime.o $(BUILD)/mediasmartctl.o
	$(CXX) $(LDFLAGS) $^ -o $@

install: mediasmartserverd mediasmartctl
	install -d $(DESTDIR)$(SBINDIR)
	install -m 0755 mediasmartserverd mediasmartctl $(DESTDIR)$(SBINDIR)/

$(BUILD)/tests: tests/tests.cpp $(OBJECTS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF $(BUILD)/tests.d $< $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/helper-fixture: tests/helper_fixture.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) -o $@

test: $(BUILD)/tests $(BUILD)/helper-fixture
	./$(BUILD)/tests $(abspath $(BUILD)/helper-fixture)

test-sanitize:
	$(MAKE) BUILD=build/sanitize CXXFLAGS='-O1 -g -std=c++17 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' LDFLAGS='-fsanitize=address,undefined -no-pie' test

clean:
	$(RM) -r build
	$(RM) *.o mediasmartserverd mediasmartctl core

-include $(OBJECTS:.o=.d) $(BUILD)/mediasmartserverd.d $(BUILD)/mediasmartctl.d $(BUILD)/tests.d

prepare-for-packaging:
	@if [ "$(PACKAGE_VERSION)" != "" ]; then \
		cd ..; \
		mkdir mediasmartserver; \
		cp -RLv mediasmartserverd/{LICENSE,Makefile,readme.txt,README.md,ABOUT.MD,improvement-plan.md,src,debian,etc,lib,tests,docs} mediasmartserver; \
		tar cfz mediasmartserver-$(PACKAGE_VERSION).tar.gz mediasmartserver; \
		rm -rf mediasmartserver; \
		bzr dh-make mediasmartserver $(PACKAGE_VERSION) mediasmartserver-$(PACKAGE_VERSION).tar.gz; \
		rm -rf mediasmartserver/debian/{*.ex,*.EX,README.Debian,README.source}; \
		cd mediasmartserver; \
		bzr commit -m "Packaging version: $(PACKAGE_VERSION)"; \
	fi

package-unsigned: prepare-for-packaging
	@if [ "$(PACKAGE_VERSION)" != "" ]; then \
		cd ../mediasmartserver; \
		bzr builddeb -- -us -uc; \
	fi

package-signed: prepare-for-packaging
	@if [ "$(PACKAGE_VERSION)" != "" ]; then \
		cd ../mediasmartserver; \
		bzr builddeb -S; \
	fi

# When rejected because of *.orig.tar.gz:
#    1) Download the pristine original tarball
#    2) cd ../mediasmartserver;
#    3) debuild -S


$(BUILD)/benchmark: tests/benchmark.cpp $(OBJECTS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

.PHONY: benchmark
benchmark: $(BUILD)/benchmark
	./$(BUILD)/benchmark before
	./$(BUILD)/benchmark after

$(BUILD)/privilege-check: tests/privilege_check.cpp $(BUILD)/runtime.o $(BUILD)/helper_process.o | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

.PHONY: test-cli
test-cli: mediasmartserverd
	python3 tests/cli_test.py ./mediasmartserverd
