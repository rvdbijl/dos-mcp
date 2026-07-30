WATCOM ?= /opt/watcom
WCL := $(WATCOM)/binl64/wcl
WASM := $(WATCOM)/binl64/wasm
DOS_INCLUDE := $(WATCOM)/h
BUILD := build

WCL_ENV = WATCOM=$(WATCOM) \
	PATH=$(WATCOM)/binl64:$(WATCOM)/binl:$(PATH) \
	EDPATH=$(WATCOM)/eddat \
	INCLUDE=$(DOS_INCLUDE)

.PHONY: all clean proto-test agent

all: proto-test agent

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

clean:
	rm -f $(BUILD)/*.EXE $(BUILD)/*.obj
