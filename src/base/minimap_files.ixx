export module MinimapFiles;

import std;
import types;
import BinaryWriter;
import Hierarchy;
import Terrain;
import Texture;
import Units;

// Warcraft III 3.0 crashes on maps without war3mapMap.blp, both when listing the map and when loading it,
// so every save writes the minimap image and its icon file like the World Editor does.

constexpr int minimap_size = 256;

// Player colors for the start location icons, as BGRA
constexpr std::array<u32, 24> player_colors = {
	0xFFFF0303, 0xFF0042FF, 0xFF1CE6B9, 0xFF540081, 0xFFFFFC00, 0xFFFE8A0E, 0xFF20C000, 0xFFE55BB0,
	0xFF959697, 0xFF7EBFF1, 0xFF106246, 0xFF4A2A04, 0xFF9B0000, 0xFF0000C3, 0xFF00EAFF, 0xFFBE00FE,
	0xFFEBCD87, 0xFFF8A48B, 0xFFBFFF80, 0xFFDCB9EB, 0xFF282828, 0xFFEBF0FF, 0xFF00781E, 0xFFA46F33,
};

/// Writes war3mapMap.blp: the terrain minimap as a 256x256 paletted BLP1.
/// The palette is a 8x8x4 colour cube, plenty for the few colours of a minimap.
void save_minimap_image(const Terrain& terrain) {
	const Texture minimap = terrain.minimap_image();

	std::array<u32, 256> palette;
	for (u32 i = 0; i < 256; i++) {
		const u32 r = (i >> 5) & 0b111;
		const u32 g = (i >> 2) & 0b111;
		const u32 b = i & 0b11;
		// BGRA with alpha 0, as the game expects in BLP palettes
		palette[i] = (r * 255 / 7) << 16 | (g * 255 / 7) << 8 | (b * 255 / 3);
	}

	std::vector<u8> indices(minimap_size * minimap_size);
	for (int y = 0; y < minimap_size; y++) {
		for (int x = 0; x < minimap_size; x++) {
			const int sx = std::min(x * minimap.width / minimap_size, minimap.width - 1);
			const int sy = std::min(y * minimap.height / minimap_size, minimap.height - 1);
			const u8* pixel = &minimap.data[(sy * minimap.width + sx) * 4];
			const u32 r = (pixel[0] * 7 + 127) / 255;
			const u32 g = (pixel[1] * 7 + 127) / 255;
			const u32 b = (pixel[2] * 3 + 127) / 255;
			indices[y * minimap_size + x] = static_cast<u8>(r << 5 | g << 2 | b);
		}
	}

	BinaryWriter writer;
	writer.write_string("BLP1");
	writer.write<u32>(1); // paletted
	writer.write<u32>(0); // no alpha
	writer.write<u32>(minimap_size);
	writer.write<u32>(minimap_size);
	writer.write<u32>(5); // extra, as the World Editor writes it
	writer.write<u32>(0); // no mipmaps

	const u32 header_size = 4 + 4 * 6 + 16 * 4 * 2 + 256 * 4;
	std::array<u32, 16> offsets {};
	std::array<u32, 16> sizes {};
	offsets[0] = header_size;
	sizes[0] = static_cast<u32>(indices.size());
	for (const u32 offset : offsets) {
		writer.write<u32>(offset);
	}
	for (const u32 size : sizes) {
		writer.write<u32>(size);
	}
	for (const u32 colour : palette) {
		writer.write<u32>(colour);
	}
	writer.write_vector(indices);

	hierarchy.map_file_write("war3mapMap.blp", writer.buffer);
}

/// Writes war3map.mmp: the minimap icons, a start location marker per player
void save_minimap_icons(const Terrain& terrain, const Units& units) {
	struct Icon {
		u32 x;
		u32 y;
		u32 colour;
	};
	std::vector<Icon> icons;
	for (const auto& unit : units.units) {
		if (unit.id != "sloc") {
			continue;
		}
		const float x = unit.position.x / static_cast<float>(terrain.width - 1);
		const float y = 1.f - unit.position.y / static_cast<float>(terrain.height - 1);
		icons.push_back({
			static_cast<u32>(std::clamp(x * minimap_size, 0.f, minimap_size - 1.f)),
			static_cast<u32>(std::clamp(y * minimap_size, 0.f, minimap_size - 1.f)),
			player_colors[std::clamp(unit.player, 0, 23)],
		});
	}

	BinaryWriter writer;
	writer.write<u32>(0); // version
	writer.write<u32>(static_cast<u32>(icons.size()));
	for (const auto& icon : icons) {
		writer.write<u32>(2); // player start location
		writer.write<u32>(icon.x);
		writer.write<u32>(icon.y);
		writer.write<u32>(icon.colour);
	}
	hierarchy.map_file_write("war3map.mmp", writer.buffer);
}

export void save_minimap_files(const Terrain& terrain, const Units& units) {
	save_minimap_image(terrain);
	save_minimap_icons(terrain, units);
}
