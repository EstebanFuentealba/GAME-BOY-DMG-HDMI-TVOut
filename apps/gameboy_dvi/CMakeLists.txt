# Replace TMDS with 10 bit UART (same baud rate):
# add_definitions(-DDVI_SERIAL_DEBUG=1)
# add_definitions(-DRUN_FROM_CRYSTAL)

add_executable(gameboy_dvi
	main.c
	print_bridge.c
)

pico_generate_pio_header(gameboy_dvi ${CMAKE_CURRENT_LIST_DIR}/video_capture.pio)

target_compile_options(gameboy_dvi PRIVATE -Wall)

target_compile_definitions(gameboy_dvi PRIVATE
	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
	)

target_link_libraries(gameboy_dvi
	pico_stdlib
	pico_multicore
	pico_util
	libdvi
	hardware_pio
	hardware_dma
	hardware_gpio
	hardware_irq
	hardware_uart
	hardware_vreg
)

# create map/bin/hex file etc.
pico_add_extra_outputs(gameboy_dvi)
