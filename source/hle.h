/**
 * @file hle.h
 * @brief Canonical storage path constants for PS Vita high-level emulation.
 */
#pragma once

#ifndef DATA_PATH
#define DATA_PATH "ux0:data/brothersinarmshourofheroes/"
#endif

#define BUNDLE_NAME "BIA.app"           // iOS bundle directory name used in the game's own paths
#define EXEC_NAME   "BIA"               // Mach-O executable inside the bundle
#define BUNDLE_PATH DATA_PATH "game_app/"
#define DOCS_PATH   DATA_PATH "saves/"
#define LOGS_PATH   DATA_PATH "logs/"
