#include <common/session.h>
#include <common/util.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

#pragma warning(push, 0)
#include <google/protobuf/text_format.h>
#include <common/trance.pb.cc>
#pragma warning(pop)

namespace
{
  trance_pb::Colour make_colour(float r, float g, float b, float a)
  {
    trance_pb::Colour colour;
    colour.set_r(r / 255);
    colour.set_g(g / 255);
    colour.set_b(b / 255);
    colour.set_a(a / 255);
    return colour;
  }

  std::string split_text_line(const std::string& text)
  {
    // Split strings into two lines at the space closest to the middle. This is
    // sort of ad-hoc. There should probably be a better way that can judge length
    // and split over more than two lines.
    auto l = text.length() / 2;
    auto r = l;
    while (true) {
      if (text[r] == ' ') {
        return text.substr(0, r) + '\n' + text.substr(r + 1);
      }

      if (text[l] == ' ') {
        return text.substr(0, l) + '\n' + text.substr(l + 1);
      }

      if (l == 0 || r == text.length() - 1) {
        break;
      }
      --l;
      ++r;
    }
    return text;
  }

  void set_default_visual_types(trance_pb::Program& program)
  {
    program.clear_visual_type();
    auto add = [&](trance_pb::Program::VisualType type_enum) {
      auto type = program.add_visual_type();
      type->set_type(type_enum);
      type->set_random_weight(1);
    };

    add(trance_pb::Program_VisualType_ACCELERATE);
    add(trance_pb::Program_VisualType_SLOW_FLASH);
    add(trance_pb::Program_VisualType_SUB_TEXT);
    add(trance_pb::Program_VisualType_FLASH_TEXT);
    add(trance_pb::Program_VisualType_PARALLEL);
    add(trance_pb::Program_VisualType_SUPER_PARALLEL);
    add(trance_pb::Program_VisualType_ANIMATION);
    add(trance_pb::Program_VisualType_SUPER_FAST);
  }

  void set_default_program(trance_pb::Session& session, const std::string& name)
  {
    auto& program = (*session.mutable_program_map())[name];
    set_default_visual_types(program);
    program.set_global_fps(120);
    program.set_zoom_intensity(.5f);
    *program.mutable_spiral_colour_a() = make_colour(255, 150, 200, 50);
    *program.mutable_spiral_colour_b() = make_colour(0, 0, 0, 50);
    program.set_reverse_spiral_direction(false);

    *program.mutable_main_text_colour() = make_colour(255, 150, 200, 224);
    *program.mutable_shadow_text_colour() = make_colour(0, 0, 0, 192);
  }

  void set_default_playlist(trance_pb::Session& session, const std::string& program)
  {
    (*session.mutable_playlist())["default"].mutable_standard()->set_program(program);
  }

  void validate_colour(trance_pb::Colour& colour)
  {
    colour.set_r(std::max(0.f, std::min(1.f, colour.r())));
    colour.set_g(std::max(0.f, std::min(1.f, colour.g())));
    colour.set_b(std::max(0.f, std::min(1.f, colour.b())));
    colour.set_a(std::max(0.f, std::min(1.f, colour.a())));
  }

  void validate_program(trance_pb::Program& program, const trance_pb::Session& session)
  {
    for (const auto& deprecated_theme : program.enabled_theme_name()) {
      auto t = program.add_enabled_theme();
      t->set_theme_name(deprecated_theme);
      t->set_random_weight(1);
    }
    program.clear_enabled_theme_name();

    uint32_t count = 0;
    std::string pinned;
    for (auto& theme : *program.mutable_enabled_theme()) {
      if (session.theme_map().find(theme.theme_name()) != session.theme_map().end()) {
        count += theme.random_weight();
        if (theme.pinned()) {
          if (pinned.empty()) {
            pinned = theme.theme_name();
          } else {
            theme.set_pinned(false);
          }
        }
      } else {
        theme.set_random_weight(0);
        theme.set_pinned(false);
      }
    }
    if (!count) {
      program.clear_enabled_theme();
      if (!pinned.empty()) {
        auto t = program.add_enabled_theme();
        t->set_theme_name(pinned);
        t->set_random_weight(1);
      } else
        for (const auto& pair : session.theme_map()) {
          auto t = program.add_enabled_theme();
          t->set_theme_name(pair.first);
          t->set_random_weight(1);
        }
    }
    count = 0;
    for (const auto& type : program.visual_type()) {
      count += type.random_weight();
    }
    if (!count) {
      set_default_visual_types(program);
    }
    program.set_global_fps(std::max(1u, std::min(240u, program.global_fps())));
    program.set_zoom_intensity(std::max(0.f, std::min(1.f, program.zoom_intensity())));
    validate_colour(*program.mutable_spiral_colour_a());
    validate_colour(*program.mutable_spiral_colour_b());
    validate_colour(*program.mutable_main_text_colour());
    validate_colour(*program.mutable_shadow_text_colour());
  }

  void validate_playlist_item(trance_pb::PlaylistItem& playlist_item, trance_pb::Session& session)
  {
    if (!playlist_item.program().empty() || playlist_item.play_time_seconds()) {
      playlist_item.mutable_standard()->set_program(playlist_item.program());
      playlist_item.mutable_standard()->set_play_time_seconds(playlist_item.play_time_seconds());
      playlist_item.clear_program();
      playlist_item.clear_play_time_seconds();
    }
    if (playlist_item.has_standard()) {
      auto it = session.program_map().find(playlist_item.standard().program());
      if (it == session.program_map().end()) {
        set_default_program(session, playlist_item.standard().program());
      }
    }
    if (playlist_item.has_subroutine()) {
      auto& subroutine = *playlist_item.mutable_subroutine();
      for (auto it = subroutine.mutable_playlist_item_name()->begin();
           it != subroutine.mutable_playlist_item_name()->end();) {
        auto jt = session.playlist().find(*it);
        if (jt == session.playlist().end()) {
          it = subroutine.mutable_playlist_item_name()->erase(it);
        } else {
          ++it;
        }
      }
    }

    for (auto it = playlist_item.mutable_next_item()->begin();
         it != playlist_item.mutable_next_item()->end();) {
      if (it->random_weight() == 0 ||
          session.playlist().find(it->playlist_item_name()) == session.playlist().end()) {
        it = playlist_item.mutable_next_item()->erase(it);
      } else {
        ++it;
      }
    }

    for (auto& next_item : *playlist_item.mutable_next_item()) {
      auto variable_it = session.variable_map().find(next_item.condition_variable_name());
      if (variable_it == session.variable_map().end()) {
        next_item.clear_condition_variable_name();
        next_item.clear_condition_variable_value();
      } else {
        auto& data = variable_it->second.value();
        if (std::find(data.begin(), data.end(), next_item.condition_variable_value()) ==
            data.end()) {
          next_item.clear_condition_variable_name();
          next_item.clear_condition_variable_value();
        }
      }
    }
  }

  void validate_variable(trance_pb::Variable& variable)
  {
    if (!variable.value_size()) {
      variable.add_value("Default");
    }
    bool found_default = false;
    for (const auto& value : variable.value()) {
      if (value == variable.default_value()) {
        found_default = true;
      }
    }
    if (!found_default) {
      variable.set_default_value(variable.value(0));
    }
  }

  template <typename T>
  T load_proto(const std::string& path)
  {
    T proto;
    std::ifstream f{path};
    if (f) {
      std::string str{std::istreambuf_iterator<char>{f}, std::istreambuf_iterator<char>{}};
      if (google::protobuf::TextFormat::ParseFromString(str, &proto)) {
        return proto;
      }
    }
    throw std::runtime_error("couldn't load " + path);
  }

  void save_proto(const google::protobuf::Message& proto, const std::string& path)
  {
    std::string str;
    google::protobuf::TextFormat::PrintToString(proto, &str);
    std::ofstream f{path};
    f << str;
  }

  std::filesystem::path make_relative(const std::filesystem::path& from, const std::filesystem::path& to)
  {
    auto cfrom = std::filesystem::canonical(from);
    auto cto = std::filesystem::canonical(to);

    auto from_it = cfrom.begin();
    auto to_it = cto.begin();
    while (from_it != cfrom.end() && to_it != cto.end() && *from_it == *to_it) {
      ++from_it;
      ++to_it;
    }
    if (from_it != cfrom.end()) {
      return to;
    }
    std::filesystem::path result = ".";
    while (to_it != cto.end()) {
      result /= *to_it;
      ++to_it;
    }
    return result;
  }

} // anonymous namespace

std::string make_relative(const std::string& from, const std::string& to)
{
  return make_relative(std::filesystem::path{from}, std::filesystem::path{to}).string();
}

bool is_image(const std::string& path)
{
  return ext_is(path, "png") || ext_is(path, "bmp") || ext_is(path, "jpg") || ext_is(path, "jpeg");
}

bool is_animation(const std::string& path)
{
  // Should really check is_gif_animated(), but it takes far too long.
  return ext_is(path, "webm") || ext_is(path, "gif");
}

bool is_font(const std::string& path)
{
  return ext_is(path, "ttf");
}

bool is_text_file(const std::string& path)
{
  return ext_is(path, "txt");
}

bool is_audio_file(const std::string& path)
{
  return ext_is(path, "wav") || ext_is(path, "ogg") || ext_is(path, "flac") || ext_is(path, "aiff");
}

bool is_enabled(const trance_pb::PlaylistItem_NextItem& next,
                const std::map<std::string, std::string>& variables)
{
  if (next.condition_variable_name().empty()) {
    return true;
  }
  std::string value;
  auto it = variables.find(next.condition_variable_name());
  if (it != variables.end()) {
    value = it->second;
  }
  return value == next.condition_variable_value();
};

void search_resources(trance_pb::Session& session, const std::string& root)
{
  static const std::string wildcards = "/wildcards/";
  auto& themes = *session.mutable_theme_map();

  std::filesystem::path root_path(root);
  for (auto it = std::filesystem::recursive_directory_iterator(root_path);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (std::filesystem::is_regular_file(it->status())) {
      auto relative_path = make_relative(root_path, it->path());
      auto jt = ++relative_path.begin();
      if (jt == relative_path.end()) {
        continue;
      }
      auto theme_name = jt == --relative_path.end() ? wildcards : jt->string();

      auto rel_str = relative_path.string();
      if (is_font(rel_str)) {
        themes[theme_name].add_font_path(rel_str);
      } else if (is_text_file(rel_str)) {
        std::ifstream f(it->path());
        std::string line;
        while (std::getline(f, line)) {
          if (!line.length()) {
            continue;
          }
          for (auto& c : line) {
            c = toupper(c);
          }
          themes[theme_name].add_text_line(split_text_line(line));
        }
      } else if (is_animation(rel_str)) {
        themes[theme_name].add_animation_path(rel_str);
      } else if (is_image(rel_str)) {
        themes[theme_name].add_image_path(rel_str);
      }
    }
  }

  // Merge wildcards theme into all others.
  for (auto& pair : themes) {
    if (pair.first == wildcards) {
      continue;
    }
    for (const auto& s : themes[wildcards].image_path()) {
      pair.second.add_image_path(s);
    }
    for (const auto& s : themes[wildcards].animation_path()) {
      pair.second.add_animation_path(s);
    }
    for (const auto& s : themes[wildcards].font_path()) {
      pair.second.add_font_path(s);
    }
    for (const auto& s : themes[wildcards].text_line()) {
      pair.second.add_text_line(s);
    }
  }

  // Leave wildcards theme if there are no others.
  themes.erase("default");
  if (themes.size() == 1) {
    themes["default"] = themes[wildcards];
  }
  themes.erase(wildcards);
  set_default_playlist(session, "default");
  auto& program = (*session.mutable_program_map())["default"];
  for (auto& pair : themes) {
    program.add_enabled_theme_name(pair.first);
  }
  session.set_first_playlist_item("default");
}

void search_resources(trance_pb::Theme& theme, const std::string& root)
{
  std::filesystem::path root_path(root);
  for (auto it = std::filesystem::recursive_directory_iterator(root_path);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (std::filesystem::is_regular_file(it->status())) {
      auto relative_path = make_relative(root_path, it->path());
      auto jt = ++relative_path.begin();
      if (jt == relative_path.end()) {
        continue;
      }
      auto rel_str = relative_path.string();
      if (is_font(rel_str)) {
        theme.add_font_path(rel_str);
      } else if (is_animation(rel_str)) {
        theme.add_animation_path(rel_str);
      } else if (is_image(rel_str)) {
        theme.add_image_path(rel_str);
      }
    }
  }
}

void search_audio_files(std::vector<std::string>& files, const std::string& root)
{
  std::filesystem::path root_path(root);
  for (auto it = std::filesystem::recursive_directory_iterator(root_path);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (std::filesystem::is_regular_file(it->status())) {
      auto relative_path = make_relative(root_path, it->path());
      auto jt = ++relative_path.begin();
      if (jt == relative_path.end()) {
        continue;
      }
      auto rel_str = relative_path.string();
      if (is_audio_file(rel_str)) {
        files.push_back(rel_str);
      }
    }
  }
}

trance_pb::System load_system(const std::string& path)
{
  auto system = load_proto<trance_pb::System>(path);
  validate_system(system);
  return system;
}

void save_system(const trance_pb::System& system, const std::string& path)
{
  save_proto(system, path);
}

trance_pb::System get_default_system()
{
  trance_pb::System system;
  system.set_enable_vsync(true);
  system.set_renderer(trance_pb::System::MONITOR);
  system.mutable_draw_depth()->set_draw_depth(.5f);
  system.mutable_eye_spacing()->set_eye_spacing(1.f / 16);
  system.set_image_cache_size(64);
  system.set_animation_buffer_size(32);
  system.set_font_cache_size(8);

  auto& export_settings = *system.mutable_last_export_settings();
  export_settings.set_width(1280);
  export_settings.set_height(720);
  export_settings.set_fps(30);
  export_settings.set_length(60);
  export_settings.set_quality(2);
  export_settings.set_threads(4);

  return system;
}

void validate_system(trance_pb::System& system)
{
  if (!system.has_draw_depth()) {
    system.mutable_draw_depth()->set_draw_depth(.5f);
  }
  system.mutable_draw_depth()->set_draw_depth(
      std::max(0.f, std::min(1.f, system.draw_depth().draw_depth())));
  if (!system.has_eye_spacing()) {
    system.mutable_eye_spacing()->set_eye_spacing(1.f / 16);
  }
  system.mutable_eye_spacing()->set_eye_spacing(
      std::max(-1.f, std::min(1.f, system.eye_spacing().eye_spacing())));
  system.set_image_cache_size(std::max(16u, system.image_cache_size()));
  system.set_animation_buffer_size(std::max(8u, system.animation_buffer_size()));
  system.set_font_cache_size(std::max(2u, system.font_cache_size()));
}

trance_pb::Session load_session(const std::string& path)
{
  auto session = load_proto<trance_pb::Session>(path);
  validate_session(session);
  return session;
}

void save_session(const trance_pb::Session& session, const std::string& path)
{
  save_proto(session, path);
}

trance_pb::Session get_default_session()
{
  trance_pb::Session session;
  set_default_playlist(session, "default");
  set_default_program(session, "default");
  validate_session(session);
  return session;
}

void validate_session(trance_pb::Session& session)
{
  if (session.theme_map().empty()) {
    (*session.mutable_theme_map())["default"];
  }
  if (session.playlist().empty() && session.program_map().empty()) {
    set_default_playlist(session, "default");
    set_default_program(session, "default");
  }
  if (session.playlist().empty()) {
    set_default_playlist(session, session.program_map().begin()->first);
  }
  if (session.program_map().empty()) {
    set_default_program(session, "default");
  }
  for (auto& pair : *session.mutable_variable_map()) {
    validate_variable(pair.second);
  }
  for (auto& pair : *session.mutable_playlist()) {
    validate_playlist_item(pair.second, session);
  }
  for (auto& pair : *session.mutable_program_map()) {
    validate_program(pair.second, session);
  }
  auto it = session.playlist().find(session.first_playlist_item());
  if (it == session.playlist().end()) {
    session.set_first_playlist_item(session.playlist().begin()->first);
  }
}
