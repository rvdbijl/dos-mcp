WATCOM ?= /opt/watcom
WCL := $(WATCOM)/binl64/wcl
WASM := $(WATCOM)/binl64/wasm
DOS_INCLUDE := $(WATCOM)/h
BUILD := build

WCL_ENV = WATCOM=$(WATCOM) \
	PATH=$(WATCOM)/binl64:$(WATCOM)/binl:$(PATH) \
	EDPATH=$(WATCOM)/eddat \
	INCLUDE=$(DOS_INCLUDE)

.PHONY: all clean proto-test agent tsr tsr-test-host

all: proto-test agent tsr tsr-test-host

$(BUILD):
	mkdir -p $(BUILD)

proto-test: | $(BUILD)
	$(WCL_ENV) $(WCL) -q -0 -ms -os -s -wx \
		-i=include -fe=$(BUILD)/PROTOCHK.EXE \
		tests/proto_test.c agent/dmproto.c

$(BUILD)/dmpacket_rx.obj: agent/dmpacket_rx.asm | $(BUILD)
	$(WCL_ENV) $(WASM) -q -fo=$@ $<

agent: $(BUILD)/dmpacket_rx.obj | $(BUILD)
	$(WCL_ENV) $(WCL) -q -0 -ms -os -s -wx \
		-i=include -fe=$(BUILD)/RAGENT.EXE \
		agent/ragent.c agent/dmproto.c agent/dmpacket.c \
		$(BUILD)/dmpacket_rx.obj agent/dmnet.c

$(BUILD)/ratsr_hooks.obj: agent/ratsr_hooks.asm | $(BUILD)
	$(WCL_ENV) $(WASM) -q -fo=$@ $<

tsr: $(BUILD)/dmpacket_rx.obj $(BUILD)/ratsr_hooks.obj | $(BUILD)
	$(WCL_ENV) $(WCL) -q -0 -ms -os -s -wx -dRA_TSR \
		-i=include -fe=$(BUILD)/RA-TSR.EXE \
		agent/ragent.c agent/dmproto.c agent/dmpacket.c \
		$(BUILD)/dmpacket_rx.obj $(BUILD)/ratsr_hooks.obj agent/dmnet.c

tsr-test-host: | $(BUILD)
	$(WCL_ENV) $(WCL) -q -0 -ms -os -s -wx \
		-fe=$(BUILD)/TSRHOST.EXE tests/tsr_host.c

clean:
	rm -f $(BUILD)/*.EXE $(BUILD)/*.obj
