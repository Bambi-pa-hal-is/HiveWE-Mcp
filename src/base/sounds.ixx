export module Sounds;

import std;
import types;
import BinaryReader;
import BinaryWriter;
import Hierarchy;

export struct Sound {
	std::string name;
	std::string file;
	std::string eax_effect;
	// int flags;
	bool looping;
	bool is_3d;
	bool stop_out_of_range;
	bool music;

	int fade_in_rate;
	int fade_out_rate;
	int volume;
	float pitch;
	float pitch_variance;
	int priority;
	int channel;
	float min_distance;
	float max_distance;
	float distance_cutoff;
	float cone_inside;
	float cone_outside;
	int cone_outside_volume;
	float cone_orientation_x;
	float cone_orientation_y;
	float cone_orientation_z;

	/// The fields version 2 and 3 files add. Kept as read so saving doesn't lose them.
	std::array<std::string, 3> extra_strings_a;
	int extra_int_a = -1;
	std::string extra_string_b;
	int extra_int_b = 0;
	std::string extra_string_c;
	int extra_int_c = 0;
	std::array<std::string, 4> extra_strings_d;
	int extra_int_d = 0;
};

export class Sounds {
  public:
	std::vector<Sound> sounds;

	void load() {
		BinaryReader reader = hierarchy.map_file_read("war3map.w3s").value();

		int version = reader.read<u32>();
		if (version != 1 && version != 2 && version != 3) {
			std::cout << "Unknown war3map.w3s version: " << version << " Attempting to load but may crash\n";
		}

		sounds.resize(reader.read<u32>());
		for (auto& i : sounds) {
			i.name = reader.read_c_string();
			i.file = reader.read_c_string();
			i.eax_effect = reader.read_c_string();
			u32 flags = reader.read<u32>();
			i.looping = flags & 0b00000001;
			i.is_3d = flags & 0b00000010;
			i.stop_out_of_range = flags & 0b00000100;
			i.music = flags & 0b00001000;

			i.fade_in_rate = reader.read<u32>();
			i.fade_out_rate = reader.read<u32>();
			i.volume = reader.read<u32>();
			i.pitch = reader.read<float>();
			i.pitch_variance = reader.read<float>();
			i.priority = reader.read<u32>();
			i.channel = reader.read<u32>();
			i.min_distance = reader.read<float>();
			i.max_distance = reader.read<float>();
			i.distance_cutoff = reader.read<float>();
			i.cone_inside = reader.read<float>();
			i.cone_outside = reader.read<float>();
			i.cone_outside_volume = reader.read<u32>();
			i.cone_orientation_x = reader.read<float>();
			i.cone_orientation_y = reader.read<float>();
			i.cone_orientation_z = reader.read<float>();

			if (version >= 2) {
				for (auto& string : i.extra_strings_a) {
					string = reader.read_c_string();
				}
				i.extra_int_a = reader.read<u32>();
				i.extra_string_b = reader.read_c_string();
				i.extra_int_b = reader.read<u32>();
				i.extra_string_c = reader.read_c_string();
				i.extra_int_c = reader.read<u32>();

				for (auto& string : i.extra_strings_d) {
					string = reader.read_c_string();
				}
				if (version >= 3) {
					i.extra_int_d = reader.read<u32>();
				}
			}
		}
	}

	static constexpr u32 write_version = 3;

	/// Writes war3map.w3s, the inverse of load()
	void save() const {
		BinaryWriter writer;
		writer.write<u32>(write_version);
		writer.write<u32>(static_cast<u32>(sounds.size()));
		for (const auto& i : sounds) {
			writer.write_c_string(i.name);
			writer.write_c_string(i.file);
			writer.write_c_string(i.eax_effect);
			const u32 flags = (i.looping ? 0b0001 : 0) | (i.is_3d ? 0b0010 : 0) | (i.stop_out_of_range ? 0b0100 : 0) | (i.music ? 0b1000 : 0);
			writer.write<u32>(flags);
			writer.write<u32>(i.fade_in_rate);
			writer.write<u32>(i.fade_out_rate);
			writer.write<u32>(i.volume);
			writer.write<float>(i.pitch);
			writer.write<float>(i.pitch_variance);
			writer.write<u32>(i.priority);
			writer.write<u32>(i.channel);
			writer.write<float>(i.min_distance);
			writer.write<float>(i.max_distance);
			writer.write<float>(i.distance_cutoff);
			writer.write<float>(i.cone_inside);
			writer.write<float>(i.cone_outside);
			writer.write<u32>(i.cone_outside_volume);
			writer.write<float>(i.cone_orientation_x);
			writer.write<float>(i.cone_orientation_y);
			writer.write<float>(i.cone_orientation_z);

			for (const auto& string : i.extra_strings_a) {
				writer.write_c_string(string);
			}
			writer.write<u32>(i.extra_int_a);
			writer.write_c_string(i.extra_string_b);
			writer.write<u32>(i.extra_int_b);
			writer.write_c_string(i.extra_string_c);
			writer.write<u32>(i.extra_int_c);
			for (const auto& string : i.extra_strings_d) {
				writer.write_c_string(string);
			}
			writer.write<u32>(i.extra_int_d);
		}
		hierarchy.map_file_write("war3map.w3s", writer.buffer);
	}
};
