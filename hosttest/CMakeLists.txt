# hosttest: el loader de Brothers In Arms: Hour of Heroes (../source, sin cambios) para Linux ARM32
# softfp (Debian armel), con psp2/kubridge falsos y vitaGL sobre GLES 1.1 de Mesa.
# Lo compila y ejecuta run.sh en Docker; ver "Prueba-ARM32-sin-consola.md".
cmake_minimum_required(VERSION 3.16)
project(biahoh-hosttest C ASM)

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/../source")
file(GLOB LOADER_SOURCES "${SRC}/*.c")
list(FILTER LOADER_SOURCES EXCLUDE REGEX "/(\\._[^/]*|vitagl_no_splash\\.c|video_avc\\.c)$")   # parche de vitaGL; AppleDouble; video_avc.c necesita SceAvcdec (video_fake.c)

add_executable(brothersinarmshourofheroes ${LOADER_SOURCES} "${SRC}/asm.S"
  psp2_fake.c vitagl_fake.c window_sdl.c video_fake.c gl_thread.c)
# include/ primero: su vitaGL.h sustituye al de vendor/vitaGL
target_include_directories(brothersinarmshourofheroes PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${PSP2_INCLUDE}" "${SRC}" /usr/include/freetype2)
# Como en la Vita: ABI softfp del código iOS, r9 reservado, Cortex-A9 con NEON.
set(FLAGS -march=armv7-a -mfpu=neon -mfloat-abi=softfp -ffixed-r9)
target_compile_options(brothersinarmshourofheroes PRIVATE ${FLAGS} -O2 -g -w)
target_compile_definitions(brothersinarmshourofheroes PRIVATE SKIP_SPLASHSCREEN DEBUG_LOADER _GNU_SOURCE
  DATA_PATH="ux0:data/brothersinarmshourofheroes/")
# Sin _TIME_BITS=64 (ojo: Debian trixie armel ya trae time_t de 64 bits; darwin.c convierte timeval/time_t a 32 bits).
# Antes gettimeofday() pisaba la pila del Profiler (ctor 109, 0x49fc).
# usan SceIoStat.st_mtime (include/sys/stat.h)
set_source_files_properties("${SRC}/objc.c" "${SRC}/main.c" "${SRC}/input.c" PROPERTIES COMPILE_DEFINITIONS HOSTTEST_SCE_STAT)
# Ejecutable y heap (brk) en 0x81000000, como el heap de la Vita: el loader valida
# punteros del juego contra 0x81000000..0x99000000 (patch.c, darwin.c).
# libstdc++ estática como en VitaSDK: cxx.c usa vtables de construcción de std::*fstream.
target_link_options(brothersinarmshourofheroes PRIVATE ${FLAGS} -Wl,--wrap=pthread_create -no-pie -Wl,-Ttext-segment=0x81000000)
target_link_libraries(brothersinarmshourofheroes SDL2 EGL OpenGL openal freetype png16 z bz2 -Wl,-Bstatic stdc++ -Wl,-Bdynamic m pthread)
