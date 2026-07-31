WATCOM ?= /opt/watcom
WCL := $(WATCOM)/binl64/wcl
WASM := $(WATCOM)/binl64/wasm
DOS_INCLUDE := $(WATCOM)/h
BUILD := build
BIN := ../bin
BUNDLE := bundle

WCL_ENV = WATCOM=$(WATCOM) \
	PATH=$(WATCOM)/binl64:$(WATCOM)/binl:$(PATH) \
	EDPATH=$(WATCOM)/eddat \
	INCLUDE=$(DOS_INCLUDE)

.PHONY: all clean proto-test config-test agent tsr tsr-test-host bin

all: proto-test config-test agent tsr tsr-test-host

$(BUILD):
	mkdir -p $(BUILD)

proto-test: | $(BUILD)
	$(WCL_ENV) $(WCL) -q -0 -ms -os -s -wx \
		-i=include -fe=$(BUILD)/PROTOCHK.EXE \
		tests/proto_test.c agent/dmproto.c

config-test: | $(BUILD)
	$(WCL_ENV) $(WCL) -q -0 -ms -os -s -wx \
		-i=include -fe=$(BUILD)/CFGCHK.EXE \
		tests/config_test.c agent/dmconfig.c

$(BUILD)/dmpacket_rx.obj: agent/dmpacket_rx.asm | $(BUILD)
	$(WCL_ENV) $(WASM) -q -fo=$@ $<

agent: $(BUILD)/dmpacket_rx.obj | $(BUILD)
	$(WCL_ENV) $(WCL) -q -0 -ms -os -s -wx \
		-i=include -fe=$(BUILD)/RAGENT.EXE \
		agent/ragent.c agent/dmconfig.c agent/dmproto.c agent/dmpacket.c \
		$(BUILD)/dmpacket_rx.obj agent/dmnet.c

$(BUILD)/ratsr_hooks.obj: agent/ratsr_hooks.asm | $(BUILD)
	$(WCL_ENV) $(WASM) -q -fo=$@ $<

tsr: $(BUILD)/dmpacket_rx.obj $(BUILD)/ratsr_hooks.obj | $(BUILD)
	$(WCL_ENV) $(WCL) -q -0 -ms -os -s -wx -dRA_TSR \
		-i=include -fe=$(BUILD)/RA-TSR.EXE \
		agent/ragent.c agent/dmconfig.c agent/dmproto.c agent/dmpacket.c \
		$(BUILD)/dmpacket_rx.obj $(BUILD)/ratsr_hooks.obj agent/dmnet.c

tsr-test-host: | $(BUILD)
	$(WCL_ENV) $(WCL) -q -0 -ms -os -s -wx \
		-fe=$(BUILD)/TSRHOST.EXE tests/tsr_host.c

bin: all
	mkdir -p $(BIN)
	cp $(BUILD)/RAGENT.EXE $(BUILD)/RA-TSR.EXE \
		$(BUILD)/PROTOCHK.EXE $(BUILD)/CFGCHK.EXE $(BIN)/
	cp $(BUNDLE)/README.TXT $(BUNDLE)/MTCP.CFG \
		$(BUNDLE)/STARTTSR.BAT $(BUNDLE)/STOPTSR.BAT $(BIN)/
	cd $(BIN) && sha256sum RAGENT.EXE RA-TSR.EXE PROTOCHK.EXE CFGCHK.EXE \
		> MANIFEST.SHA256

clean:
	rm -f $(BUILD)/*.EXE $(BUILD)/*.obj
