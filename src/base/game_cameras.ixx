export module GameCameras;

import std;
import types;
import BinaryReader;
import BinaryWriter;
import Hierarchy;

export struct GameCamera {
	f32 target_x;
	f32 target_y;
	f32 z_offset;
	f32 rotation;
	f32 angle_of_attack;
	f32 distance;
	f32 roll;
	f32 fov;
	f32 far_z;
	f32 near_z;

	f32 local_pitch = 0.f;
	f32 local_yaw = 0.f;
	f32 local_roll = 0.f;

	f32 dof_distance = 0.f;
	f32 dof_scale = 0.f;
	f32 pos_absolute_z = 0.f;
	std::string name;
	u32 free_camera = 0;
};

export class GameCameras {
  public:
	std::vector<GameCamera> cameras;

	void load(const i32 game_version_major, const i32 game_version_minor, const f32 terrain_offset_x, const f32 terrain_offset_y) {
		BinaryReader reader = hierarchy.map_file_read("war3map.w3c").value();

		const int version = reader.read<u32>();
		if (version != 0 && version != 3) {
			std::cout << "Unknown war3map.w3c version: " << version << " Attempting to load but may crash\n";
		}

		cameras.resize(reader.read<u32>());
		for (auto& i : cameras) {
			// change coordinate system to [0, terrain_width] x [0, terrain_height]
			// as used by other objects in HiveWE
			i.target_x = (reader.read<f32>() - terrain_offset_x) / 128.f;
			i.target_y = (reader.read<f32>() - terrain_offset_y) / 128.f;
			i.z_offset = reader.read<f32>() / 128.f;

			i.rotation = reader.read<f32>();
			i.angle_of_attack = reader.read<f32>();
			i.distance = reader.read<f32>();
			i.roll = reader.read<f32>();
			i.fov = reader.read<f32>();
			i.far_z = reader.read<f32>();
			i.near_z = reader.read<f32>();

			if (game_version_major * 100 + game_version_minor >= 131) {
				i.local_pitch = reader.read<f32>();
				i.local_yaw = reader.read<f32>();
				i.local_roll = reader.read<f32>();
			}
			if (version >= 3) {
				i.dof_distance = reader.read<f32>();
				i.dof_scale = reader.read<f32>();
				i.pos_absolute_z = reader.read<f32>();
			}

			i.name = reader.read_c_string();

			if (version >= 3) {
				i.free_camera = reader.read<u32>();
			}
		}
	}

	static constexpr u32 write_version = 3;

	/// Writes war3map.w3c, the inverse of load()
	void save(const f32 terrain_offset_x, const f32 terrain_offset_y) const {
		BinaryWriter writer;
		writer.write<u32>(write_version);
		writer.write<u32>(static_cast<u32>(cameras.size()));
		for (const auto& i : cameras) {
			writer.write<f32>(i.target_x * 128.f + terrain_offset_x);
			writer.write<f32>(i.target_y * 128.f + terrain_offset_y);
			writer.write<f32>(i.z_offset * 128.f);
			writer.write<f32>(i.rotation);
			writer.write<f32>(i.angle_of_attack);
			writer.write<f32>(i.distance);
			writer.write<f32>(i.roll);
			writer.write<f32>(i.fov);
			writer.write<f32>(i.far_z);
			writer.write<f32>(i.near_z);
			writer.write<f32>(i.local_pitch);
			writer.write<f32>(i.local_yaw);
			writer.write<f32>(i.local_roll);
			writer.write<f32>(i.dof_distance);
			writer.write<f32>(i.dof_scale);
			writer.write<f32>(i.pos_absolute_z);
			writer.write_c_string(i.name);
			writer.write<u32>(i.free_camera);
		}
		hierarchy.map_file_write("war3map.w3c", writer.buffer);
	}

	void remove_camera(GameCamera* camera) {
		const auto iterator = cameras.begin() + std::distance(cameras.data(), camera);
		cameras.erase(iterator);
	}

	void remove_cameras(const std::unordered_set<GameCamera*>& list) {
		std::erase_if(cameras, [&](GameCamera& camera) {
			return list.contains(&camera);
		});
	}
};
