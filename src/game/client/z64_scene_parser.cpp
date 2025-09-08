#include "cbase.h"
#include "filesystem.h"
#include "convar.h"
#include <vector>
#include <string>

struct N64Game {
	std::string name;
	std::string path;
};

//std::string skyboxType[0x23] = { "no sky", "the standard sky thats affected by cloudy state", "the hylian bazaar", "a storm sky", "market ruins", "unused black skybox", "unused", "link's house", "unused", "market square day", "market square night", "the happy mask shop", "the know-it-all brothers house", "unused", "the kokiri twins house", "the stable", "the stew lady's house", "the kokiri shop", "unused", "the goron shop", "the zora shop", "unused", "the kakariko potion shop", "the market potion shop", "the bomb shop", "unused", "the dog lady's house", "impa's house", "the gerudo tent", "an enviromental color", "unused", "unused", "mido's house", "saria's house", "the dog guy's house"};
const char* skyboxNames[] = {
	"no sky", "the standard sky thats affected by cloudy state", "the hylian bazaar", "a storm sky", "market ruins", "unused black skybox", "unused", "link's house", "unused", "market square day", "market square night", "the happy mask shop", "the know-it-all brothers house", "unused", "the kokiri twins house", "the stable", "the stew lady's house", "the kokiri shop", "unused", "the goron shop", "the zora shop", "unused", "the kakariko potion shop", "the market potion shop", "the bomb shop", "unused", "the dog lady's house", "impa's house", "the gerudo tent", "an enviromental color", "unused", "unused", "mido's house", "saria's house", "the dog guy's house"
};
const int skyboxNamesCount = sizeof(skyboxNames) / sizeof(skyboxNames[0]);

// Helper to parse n64_games.txt
std::vector<N64Game> ParseGamesList(const char* filePath) {
	std::vector<N64Game> games;
	FileHandle_t file = g_pFullFileSystem->Open(filePath, "r", "MOD");
	if (!file) {
		Msg("[N64 Parser] Failed to open games list: %s\n", filePath);
		return games;
	}

	char line[256];
	while (g_pFullFileSystem->ReadLine(line, sizeof(line), file)) {
		char gameName[128], gamePath[128];
		if (sscanf(line, "%127[^,],%127s", gameName, gamePath) == 2) {
			games.push_back({ gameName, gamePath });
		}
	}

	g_pFullFileSystem->Close(file);
	return games;
}

// Console command: n64_parse_map <game> <map>
CON_COMMAND(z64_parse_map, "Parses a scene.zscene for the the n64 zelda titles.") {
	if (args.ArgC() != 3) {
		Msg("Usage: z64_parse_map <game> <map_folder>\n");
		return;
	}

	const char* gameName = args[1];
	const char* mapFolder = args[2];

	auto games = ParseGamesList("n64_games.txt");
	std::string basePath;
	for (const auto& game : games) {
		if (Q_stricmp(game.name.c_str(), gameName) == 0) {
			basePath = game.path;
			break;
		}
	}

	if (basePath.empty()) {
		Msg("[Z64 Parser] Game not found: %s\n", gameName);
		return;
	}

	std::string mapPath = basePath + "/" + mapFolder + "/scene.zscene";
	FileHandle_t file = g_pFullFileSystem->Open(mapPath.c_str(), "rb", "MOD");
	if (!file) {
		Msg("[Z64 Parser] Failed to open file: %s\n", mapPath.c_str());
		return;
	}

	g_pFullFileSystem->Seek(file, 0x08, FILESYSTEM_SEEK_HEAD);
	unsigned char buffer[8];
	int offset = 0x08;

	Msg("[Z64 Parser] Current game specified: %s\n", gameName);

	while (g_pFullFileSystem->Read(buffer, sizeof(buffer), file) == 8) {
		//if (gameName) isValid("Ocarina of Time");
		if (buffer[0] == 0x00) {
			Msg("[Z64 Parser] 0x00 command at offset 0x%X. TODO: Implant! [Start Position List]\n", offset);
		}
		if (buffer[0] == 0x01) {
			Msg("[Z64 Parser] 0x01 command at offset 0x%X. TODO: Implant! [Actor List]\n", offset);
		}
		/*if (buffer[0] == 0x02) {
			Msg("[Z64 Parser] TODO: Implant! [Camera's (MM Only!)]");
		}*/
		if (buffer[0] == 0x03) {
			Msg("[Z64 Parser] 0x03 command at offset 0x%X. TODO: Implant! [Collision Header]\n", offset);
		}
		if (buffer[0] == 0x04) {
			unsigned char roomCount = buffer[1];
			Msg("[Z64 Parser] Found 0x04 command at offset 0x%X. Number of rooms: %d\n", offset, roomCount);
		}
		if (buffer[0] == 0x05) {
			Msg("[Z64 Parser] 0x05 command at offset 0x%X. TODO: Implant! [Wind]\n");
		}
		if (buffer[0] == 0x06) {
			Msg("[Z64 Parser] 0x06 command at offset 0x%X. TODO: Implant! [Enterance List]\n", offset);
		}
		if (buffer[0] == 0x07) {
			Msg("[Z64 Parser] 0x07 command at offset 0x%X. TODO: Implant! [Special Objects]\n", offset);
		}
		if (buffer[0] == 0x08) {
			Msg("[Z64 Parser] 0x08 command at offset 0x%X. TODO: Implant! [Room Behavior]\n", offset);
		}
		/*if (buffer[0] == 0x09) {
			Msg("[Z64 Parser] Unused");
		}*/
		if (buffer[0] == 0x0A) {
			Msg("[Z64 Parser] 0x0A command at offset 0x%X. TODO: Implant! [Defines the Mesh]\n", offset);
		}
		if (buffer[0] == 0x0B) {
			Msg("[Z64 Parser] 0x0B command at offset 0x%X. TODO: Implant! [Object List]\n", offset);
		}
		if (buffer[0] == 0x0C) {
			Msg("[Z64 Parser] 0x0C command at offset 0x%X. TODO: Implant! [Unused Light Settings]\n", offset);
		}
		if (buffer[0] == 0x0D) {
			Msg("[Z64 Parser] 0x0D command at offset 0x%X. TODO: Implant! [Pathways]\n", offset);
		}
		if (buffer[0] == 0x0E) {
			Msg("[Z64 Parser] 0x0E command at offset 0x%X. TODO: Implant! [Transition Actor List]\n", offset);
		}
		if (buffer[0] == 0x0F) {
			Msg("[Z64 Parser] 0x0F command at offset 0x%X. TODO: Implant! [Lighting Settings]\n", offset);
		}
		if (buffer[0] == 0x10) {
			Msg("[Z64 Parser] 0x10 command at offset 0x%X. TODO: Implant! [Time Settings]\n", offset);
		}
		/*if (buffer[0] == 0x11) {
			unsigned char skyboxSetting = buffer[4];
			Msg("[Z64 Parser] Found 0x11 command at offset 0x%X. Skybox type set to %02x and is %s\n", offset, skyboxSetting, skyboxSetting + skyboxType);
		}*/
		if (buffer[0] == 0x11) {
			unsigned char skyboxSetting = buffer[4];
			int skyboxType = static_cast<int>(buffer[5]); // Assuming skyboxType is stored in buffer[5]

			int skyboxIndex = static_cast<int>(skyboxSetting) + skyboxType;

			// Ensure the index is within the valid range
			const char* skyboxName = "Invalid Skybox";
			if (skyboxIndex >= 0 && skyboxIndex < skyboxNamesCount) {
				skyboxName = skyboxNames[skyboxIndex];
			}

			Msg("[Z64 Parser] Found 0x11 command at offset 0x%X. Skybox type set to %02X and is %s\n",
				offset, skyboxSetting, skyboxName);
		}
		if (buffer[0] == 0x12) {
			Msg("[Z64 Parser] 0x12 command at offset 0x%X. TODO: Implant! [Skybox Modifier]\n", offset);
		}
		if (buffer[0] == 0x13) {
			Msg("[Z64 Parser] 0x13 command at offset 0x%X. TODO: Implant! [Exit List]\n", offset);
		}
		if (buffer[0] == 0x15) {
			Msg("[Z64 Parser] 0x15 command at offset 0x%X. TODO: Implant! [Sound Settings]\n", offset);
		}
		if (buffer[0] == 0x16) {
			Msg("[Z64 Parser] 0x16 command at offset 0x%X. TODO: Implant! [Echo]\n", offset);
		}
		if (buffer[0] == 0x17) {
			Msg("[Z64 Parser] 0x17 command at offset 0x%X. TODO: Implant! [Cutscenes]\n", offset);
		}
		if (buffer[0] == 0x18) {
			Msg("[Z64 Parser] 0x18 command at offset 0x%X. TODO: Implant! [Alternate Headers]\n", offset);
		}
		if (buffer[0] == 0x19) {
			Msg("[Z64 Parser] 0x19 command at offset 0x%X. TODO: Implant! [Camera Setting and World Map]\n", offset);
		}
		if (buffer[0] == 0x14 && buffer[1] == 0x00 && buffer[2] == 0x00 && buffer[3] == 0x00 &&
			buffer[4] == 0x00 && buffer[5] == 0x00 && buffer[6] == 0x00 && buffer[7] == 0x00) {
			Msg("[Z64 Parser] Found the end marker (0x14) at offset 0x%X.", offset);
			break;
		}

		offset += 8;
	}

	g_pFullFileSystem->Close(file);
}