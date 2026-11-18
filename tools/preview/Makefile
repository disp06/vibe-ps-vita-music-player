# Desktop preview renderer (needs cairo + freetype dev packages). Run from repo root:
#   make -C tools/preview && tools/preview/vibe_preview docs/previews
ROOT := ../..
SRC := host_main.c stubs.c $(addprefix $(ROOT)/src/,app.c ui.c font.c gfx_common.c gfx_host.c skin_metro.c skin_retro.c skin_minimal.c config.c browser.c decoder.c)
vibe_preview: $(SRC)
	$(CC) -O1 -I$(ROOT)/src -o $@ $(SRC) $$(pkg-config --cflags --libs cairo freetype2) -lm
