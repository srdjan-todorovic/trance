#include <creator/theme.h>
#include <common/common.h>
#include <common/media/image.h>
#include <common/media/streamer.h>
#include <common/session.h>
#include <creator/item_list.h>
#include <creator/main.h>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <thread>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/listctrl.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/textdlg.h>
#include <wx/timer.h>
#include <wx/treelist.h>
#include <SFML/Graphics.hpp>
#pragma warning(pop)

namespace
{
  std::mt19937 engine{static_cast<std::mt19937::result_type>(time(0))};

  std::string NlToUser(const std::string& nl)
  {
    std::string s = nl;
    std::string r;
    bool first = true;
    while (!s.empty()) {
      auto p = s.find_first_of('\n');
      auto q = s.substr(0, p != std::string::npos ? p : s.size());
      r += (first ? "" : "  /  ") + q;
      first = false;
      s = s.substr(p != std::string::npos ? 1 + p : s.size());
    }
    return r;
  }

  std::string StripUserNls(const std::string& nl)
  {
    std::string s = nl;
    std::string r;
    bool first = true;
    while (!s.empty()) {
      auto p = s.find("\\n");
      auto q = s.substr(0, p != std::string::npos ? p : s.size());
      r += (first ? "" : "/") + q;
      first = false;
      s = s.substr(p != std::string::npos ? 2 + p : s.size());
    }
    return r;
  }

  std::string UserToNl(const std::string& user)
  {
    std::string s = StripUserNls(user);
    std::string r;
    bool first = true;
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty()) {
      auto p = s.find_first_of('/');
      auto q = s.substr(0, p != std::string::npos ? p : s.size());
      while (!q.empty() && q[0] == ' ') {
        q = q.substr(1);
      }
      while (!q.empty() && q[q.size() - 1] == ' ') {
        q = q.substr(0, q.size() - 1);
      }
      if (!q.empty()) {
        r += (first ? "" : "\n") + q;
        first = false;
      }
      s = s.substr(p != std::string::npos ? 1 + p : s.size());
      p = s.find_first_of('/');
    }
    return r;
  }

  std::string GetFileSize(const std::string& path)
  {
    struct stat stat_buffer;
    int rc = stat(path.c_str(), &stat_buffer);
    auto size = rc == 0 ? stat_buffer.st_size : 0;
    if (size < 1024) {
      return std::to_string(size) + "B";
    }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1);
    if (size < 1024 * 1024) {
      ss << static_cast<float>(size) / 1024;
      return ss.str() + "KB";
    }
    if (size < 1024 * 1024 * 1024) {
      ss << static_cast<float>(size) / (1024 * 1024);
      return ss.str() + "MB";
    }
    ss << static_cast<float>(size) / (1024 * 1024 * 1024);
    return ss.str() + "GB";
  }

  std::vector<std::string> SortedPaths(const trance_pb::Theme& theme)
  {
    std::vector<std::string> paths;
    for (const auto& path : theme.image_path()) {
      paths.push_back(path);
    }
    for (const auto& path : theme.animation_path()) {
      paths.push_back(path);
    }
    for (const auto& path : theme.font_path()) {
      paths.push_back(path);
    }
    std::sort(paths.begin(), paths.end());
    return paths;
  }

  bool IsUnused(const trance_pb::Session& session, const std::string& path)
  {
    for (const auto& pair : session.theme_map()) {
      const auto& t = pair.second;
      if (std::find(t.font_path().begin(), t.font_path().end(), path) !=
              t.font_path().end() ||
          std::find(t.image_path().begin(), t.image_path().end(), path) !=
              t.image_path().end() ||
          std::find(t.animation_path().begin(), t.animation_path().end(), path) !=
              t.animation_path().end()) {
        return false;
      }
    }
    return true;
  }
}

class ImagePanel : public wxPanel
{
public:
  ImagePanel(wxWindow* parent) : wxPanel{parent, wxID_ANY}, _bitmap{1, 1}
  {
    _timer = new wxTimer(this, wxID_ANY);
    _timer->Start(20);
    SetBackgroundStyle(wxBG_STYLE_CUSTOM);

    Bind(wxEVT_TIMER,
         [&](const wxTimerEvent&) {
           if (!_streamer || _shutdown) {
             return;
           }
           auto image = _streamer->next_frame();
           if (!image) {
             _streamer->reset();
             image = _streamer->next_frame();
           }
           _image = ConvertImage(image);
           _dirty = true;
           Refresh();
         },
         wxID_ANY);

    Bind(wxEVT_SIZE, [&](const wxSizeEvent&) {
      _dirty = true;
      Refresh();
    });

    Bind(wxEVT_PAINT, [&](const wxPaintEvent&) {
      wxPaintDC dc{this};
      int width = 0;
      int height = 0;
      dc.GetSize(&width, &height);

      if (width && height && _dirty) {
        wxImage temp_image{width, height};
        if (_image) {
          auto iw = _image->GetWidth();
          auto ih = _image->GetHeight();
          auto scale = std::min(float(height) / ih, float(width) / iw);
          auto sw = unsigned(scale * iw);
          auto sh = unsigned(scale * ih);
          temp_image.Paste(_image->Scale(sw, sh, wxIMAGE_QUALITY_HIGH), (width - sw) / 2,
                           (height - sh) / 2);
        }
        _bitmap = wxBitmap{temp_image};
      }
      dc.DrawBitmap(_bitmap, 0, 0, false);
      dc.SetFont(*wxNORMAL_FONT);
      dc.SetTextForeground(*wxWHITE);
      if (!_info.empty()) {
        dc.DrawText(std::to_string(_image->GetWidth()) + "x" + std::to_string(_image->GetHeight()) +
                        " [" + _info + "]",
                    4, 4);
      }
    });
  }

  ~ImagePanel()
  {
    _timer->Stop();
  }

  void SetAnimation(std::unique_ptr<Streamer> streamer)
  {
    _streamer = std::move(streamer);
    _dirty = true;
    _frame = 0;
    Refresh();
  }

  void SetImage(const Image& image)
  {
    _image = ConvertImage(image);
    _streamer.reset();
    _dirty = true;
    _frame = 0;
    Refresh();
  }

  void SetInfo(const std::string& info)
  {
    _info = info;
  }

  void Shutdown()
  {
    _shutdown = true;
  }

private:
  std::unique_ptr<wxImage> ConvertImage(const Image& image)
  {
    auto ptr = image.get_sf_image();
    if (!ptr) {
      return {};
    }
    auto sf_image = *ptr;
    std::unique_ptr<wxImage> wx =
        std::make_unique<wxImage>((int) sf_image.getSize().x, (int) sf_image.getSize().y);
    for (unsigned y = 0; y < sf_image.getSize().y; ++y) {
      for (unsigned x = 0; x < sf_image.getSize().x; ++x) {
        const auto& c = sf_image.getPixel(x, y);
        wx->SetRGB(x, y, c.r, c.g, c.b);
      }
    }
    return wx;
  }

  bool _dirty = true;
  bool _shutdown = false;
  std::string _info;
  std::size_t _frame;
  std::unique_ptr<wxImage> _image;
  std::unique_ptr<Streamer> _streamer;
  wxBitmap _bitmap;
  wxTimer* _timer;
};

ThemePage::ThemePage(wxNotebook* parent, CreatorFrame& creator_frame, trance_pb::Session& session,
                     const std::string& session_path)
: wxNotebookPage{parent, wxID_ANY}
, _creator_frame{creator_frame}
, _session{session}
, _session_path{session_path}
, _complete_theme{new trance_pb::Theme}
, _tree{nullptr}
{
  auto sizer = new wxBoxSizer{wxVERTICAL};
  auto splitter = new wxSplitterWindow{this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxSP_THIN_SASH | wxSP_LIVE_UPDATE};
  splitter->SetSashGravity(0);
  splitter->SetMinimumPaneSize(128);

  auto bottom_panel = new wxPanel{splitter, wxID_ANY};
  auto bottom = new wxBoxSizer{wxHORIZONTAL};

  auto bottom_splitter = new wxSplitterWindow{bottom_panel, wxID_ANY, wxDefaultPosition,
                                              wxDefaultSize, wxSP_THIN_SASH | wxSP_LIVE_UPDATE};
  bottom_splitter->SetSashGravity(0.75);
  bottom_splitter->SetMinimumPaneSize(128);

  auto left_panel = new wxPanel{bottom_splitter, wxID_ANY};
  auto left = new wxBoxSizer{wxHORIZONTAL};
  auto right_panel = new wxPanel{bottom_splitter, wxID_ANY};
  auto right = new wxStaticBoxSizer{wxVERTICAL, right_panel, "Text messages"};
  auto right_buttons = new wxBoxSizer{wxVERTICAL};

  auto left_splitter = new wxSplitterWindow{left_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxSP_THIN_SASH | wxSP_LIVE_UPDATE};
  left_splitter->SetSashGravity(0.5);
  left_splitter->SetMinimumPaneSize(128);

  auto leftleft_panel = new wxPanel{left_splitter, wxID_ANY};
  auto leftleft = new wxStaticBoxSizer{wxVERTICAL, leftleft_panel, "Included files"};
  auto leftleft_row0 = new wxBoxSizer{wxHORIZONTAL};
  auto leftleft_row1 = new wxBoxSizer{wxHORIZONTAL};
  auto leftleft_row2 = new wxBoxSizer{wxHORIZONTAL};
  auto leftright_panel = new wxPanel{left_splitter, wxID_ANY};
  auto leftright = new wxStaticBoxSizer{wxVERTICAL, leftright_panel, "Preview"};

  _item_list = new ItemList<trance_pb::Theme>{
      splitter,
      *session.mutable_theme_map(),
      "theme",
      [&](const std::string& s) {
        _item_selected = s;
        auto it = _session.theme_map().find(_item_selected);
        if (it != _session.theme_map().end()) {
          _creator_frame.SetStatusText(
              _item_selected + ": " + std::to_string(it->second.image_path().size()) + " images; " +
              std::to_string(it->second.animation_path().size()) + " animations; " +
              std::to_string(it->second.font_path().size()) + " fonts.");
        }
        RefreshOurData();
      },
      std::bind(&CreatorFrame::ThemeCreated, &_creator_frame, std::placeholders::_1),
      std::bind(&CreatorFrame::ThemeDeleted, &_creator_frame, std::placeholders::_1),
      std::bind(&CreatorFrame::ThemeRenamed, &_creator_frame, std::placeholders::_1,
                std::placeholders::_2)};

  _tree = new wxTreeListCtrl{leftleft_panel, 0, wxDefaultPosition, wxDefaultSize,
                             wxTL_SINGLE | wxTL_CHECKBOX | wxTL_3STATE | wxTL_NO_HEADER};
  _tree->GetView()->SetToolTip(
      "Images, animations and fonts that are part "
      "of the currently-selected theme.");
  _tree->AppendColumn("");

  _image_panel = new ImagePanel{leftright_panel};
  _image_panel->SetToolTip("Preview of the selected item.");

  _text_list = new wxListCtrl{right_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                              wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL | wxLC_EDIT_LABELS};
  _text_list->InsertColumn(0, "Text", wxLIST_FORMAT_LEFT, wxLIST_AUTOSIZE_USEHEADER);
  _text_list->SetToolTip(
      "Text messages that are part "
      "of the currently-selected theme.");

  _button_new = new wxButton{right_panel, wxID_ANY, "New"};
  _button_edit = new wxButton{right_panel, wxID_ANY, "Edit"};
  _button_delete = new wxButton{right_panel, wxID_ANY, "Delete"};
  _button_open = new wxButton{leftleft_panel, wxID_ANY, "Show in explorer"};
  _button_rename = new wxButton{leftleft_panel, wxID_ANY, "Move / rename"};
  _button_refresh = new wxButton{leftleft_panel, wxID_ANY, "Refresh directory"};
  _button_next_unused = new wxButton{leftleft_panel, wxID_ANY, "Next unused"};
  _button_random_unused = new wxButton{leftleft_panel, wxID_ANY, "Random unused"};
  _button_next_theme = new wxButton{leftleft_panel, wxID_ANY, "Next in theme"};

  _button_show_all = new wxRadioButton{leftleft_panel,    wxID_ANY,      "All",
                                       wxDefaultPosition, wxDefaultSize, wxRB_GROUP};
  _button_show_images = new wxRadioButton{leftleft_panel, wxID_ANY, "Images"};
  _button_show_animations = new wxRadioButton{leftleft_panel, wxID_ANY, "Animations"};

  _button_new->SetToolTip("Create a new text item.");
  _button_edit->SetToolTip("Edit the selected text item.");
  _button_delete->SetToolTip("Delete the selected text item.");
  _button_open->SetToolTip("Open the file in the explorer.");
  _button_rename->SetToolTip("Move or rename the selected file or directory.");
  _button_refresh->SetToolTip(
      "Scan the session directory for available images, animations and fonts.");
  _button_next_unused->SetToolTip("Jump to the next unused item.");
  _button_random_unused->SetToolTip("Jump to a random unused item.");
  _button_next_theme->SetToolTip("Jump to the next item in the current theme.");
  _button_show_all->SetToolTip("Show all items (images, animations and fonts).");
  _button_show_images->SetToolTip("Show images only.");
  _button_show_animations->SetToolTip("Show animations only.");

  _button_open->Enable(false);
  _button_rename->Enable(false);
  _button_show_all->SetValue(true);
  _button_show_images->SetValue(false);
  _button_show_animations->SetValue(false);

  leftleft->Add(leftleft_row0, 0, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft->Add(_tree, 1, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft->Add(leftleft_row1, 0, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft->Add(leftleft_row2, 0, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft_row0->Add(_button_show_all, 0, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft_row0->Add(_button_show_images, 0, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft_row0->Add(_button_show_animations, 0, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft_row1->Add(_button_open, 1, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft_row1->Add(_button_rename, 1, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft_row1->Add(_button_refresh, 1, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft_row2->Add(_button_next_unused, 1, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft_row2->Add(_button_random_unused, 1, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft_row2->Add(_button_next_theme, 1, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftleft_panel->SetSizer(leftleft);
  leftright->Add(_image_panel, 1, wxEXPAND | wxALL, DEFAULT_BORDER);
  leftright_panel->SetSizer(leftright);
  left->Add(left_splitter, 1, wxEXPAND, 0);
  left_splitter->SplitVertically(leftleft_panel, leftright_panel);
  left_panel->SetSizer(left);
  right->Add(_text_list, 1, wxEXPAND | wxALL, DEFAULT_BORDER);
  right->Add(right_buttons, 0, wxEXPAND, 0);
  right_buttons->Add(_button_new, 0, wxEXPAND | wxALL, DEFAULT_BORDER);
  right_buttons->Add(_button_edit, 0, wxEXPAND | wxALL, DEFAULT_BORDER);
  right_buttons->Add(_button_delete, 0, wxEXPAND | wxALL, DEFAULT_BORDER);
  right_panel->SetSizer(right);
  bottom->Add(bottom_splitter, 1, wxEXPAND, 0);
  bottom_splitter->SplitVertically(left_panel, right_panel);
  bottom_panel->SetSizer(bottom);

  sizer->Add(splitter, 1, wxEXPAND, 0);
  splitter->SplitHorizontally(_item_list, bottom_panel);
  SetSizer(sizer);

  _text_list->Bind(wxEVT_SIZE,
                   [&](wxSizeEvent&) { _text_list->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER); },
                   wxID_ANY);

  _text_list->Bind(wxEVT_LIST_ITEM_SELECTED,
                   [&](wxListEvent& e) {
                     std::string user = _text_list->GetItemText(e.GetIndex()).ToStdString();
                     _current_text_line = UserToNl(user);
                     GenerateFontPreview();
                   },
                   wxID_ANY);

  _text_list->Bind(
      wxEVT_LIST_END_LABEL_EDIT,
      [&](wxListEvent& e) {
        if (e.IsEditCancelled()) {
          return;
        }
        e.Veto();
        std::string new_text = e.GetLabel().ToStdString();
        new_text = UserToNl(new_text);
        if (new_text.empty()) {
          return;
        }
        for (int i = 0; i < _text_list->GetItemCount(); ++i) {
          if (_text_list->GetItemState(i, wxLIST_STATE_SELECTED)) {
            *(*_session.mutable_theme_map())[_item_selected].mutable_text_line()->Mutable(i) =
                new_text;
          }
        }
        RefreshOurData();
        _current_text_line = new_text;
        GenerateFontPreview();
        _creator_frame.MakeDirty(true);
      },
      wxID_ANY);

  _button_new->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [&](wxCommandEvent&) {
    (*_session.mutable_theme_map())[_item_selected].add_text_line("NEW TEXT");
    RefreshOurData();
    _text_list->SetItemState(_text_list->GetItemCount() - 1, wxLIST_STATE_SELECTED,
                             wxLIST_STATE_SELECTED);
    _creator_frame.MakeDirty(true);
  });

  _button_edit->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [&](wxCommandEvent&) {
    for (int i = 0; i < _text_list->GetItemCount(); ++i) {
      if (_text_list->GetItemState(i, wxLIST_STATE_SELECTED)) {
        _text_list->EditLabel(i);
        return;
      }
    }
  });

  _button_delete->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [&](wxCommandEvent&) {
    int removed = -1;
    for (int i = 0; i < _text_list->GetItemCount(); ++i) {
      if (_text_list->GetItemState(i, wxLIST_STATE_SELECTED)) {
        auto& theme = (*_session.mutable_theme_map())[_item_selected];
        theme.mutable_text_line()->erase(i + theme.mutable_text_line()->begin());
        removed = i;
        break;
      }
    }
    RefreshOurData();
    if (removed >= 0 && _text_list->GetItemCount()) {
      _text_list->SetItemState(std::min(_text_list->GetItemCount() - 1, removed),
                               wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }
    _creator_frame.MakeDirty(true);
  });

  _button_open->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [&](wxCommandEvent&) {
    auto root = std::filesystem::path{_session_path}.parent_path().string();
    for (const auto& pair : _tree_lookup) {
      if (pair.second == _tree->GetSelection()) {
        auto path = root + "\\" + pair.first;
        _creator_frame.SetStatusText("Opening " + path);
        wxExecute("explorer /select,\"" + path + "\"", wxEXEC_ASYNC);
      }
    }
  });

  _button_rename->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [&](wxCommandEvent&) {
    auto root = std::filesystem::path{_session_path}.parent_path().string();
    std::string old_relative_path;
    for (const auto& pair : _tree_lookup) {
      if (pair.second == _tree->GetSelection()) {
        old_relative_path = pair.first;
        break;
      }
    }
    if (old_relative_path.empty()) {
      return;
    }
    std::unique_ptr<wxTextEntryDialog> dialog{new wxTextEntryDialog(
        this, "New location (relative to session directory):", "Move / rename", old_relative_path)};
    if (dialog->ShowModal() != wxID_OK) {
      return;
    }
    std::string new_relative_path = dialog->GetValue().ToStdString();
    if (new_relative_path == old_relative_path) {
      return;
    }
    auto rename_file = [&](const std::string old_abs, const std::string& new_abs) {
      auto old_path = std::filesystem::path{old_abs};
      auto new_path = std::filesystem::path{new_abs};
      std::error_code ec;
      auto parent = std::filesystem::canonical(new_path.parent_path(), ec);
      if (!std::filesystem::is_directory(parent) &&
          (ec || !std::filesystem::create_directories(parent))) {
        wxMessageBox("Couldn't create directory " + parent.string(), "", wxICON_ERROR, this);
        return false;
      }
      bool exists = std::filesystem::exists(new_path, ec);
      if (exists || ec) {
        wxMessageBox(
            "Couldn't rename " + old_path.string() + ": " + new_path.string() + " already exists",
            "", wxICON_ERROR, this);
        return false;
      }
      std::filesystem::rename(old_path, new_path, ec);
      if (ec) {
        wxMessageBox("Couldn't rename " + old_path.string() + " to " + new_path.string(), "",
                     wxICON_ERROR, this);
        return false;
      }
      for (auto& pair : *session.mutable_theme_map()) {
        auto& theme = pair.second;
        auto c = [&](google::protobuf::RepeatedPtrField<std::string>& field) {
          auto it = std::find(field.begin(), field.end(), make_relative(root, old_abs));
          if (it != field.end()) {
            *field.Add() = make_relative(root, new_abs);
          }
        };
        c(*theme.mutable_font_path());
        c(*theme.mutable_image_path());
        c(*theme.mutable_animation_path());
      }
      return true;
    };
    auto old_root = root + "/" + old_relative_path;
    auto new_root = root + "/" + new_relative_path;
    if (std::filesystem::is_regular_file(old_root)) {
      rename_file(old_root, new_root);
    } else {
      for (auto it = std::filesystem::recursive_directory_iterator(old_root);
           it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (!std::filesystem::is_regular_file(it->status())) {
          continue;
        }
        auto rel_rel = make_relative(old_root, it->path().string());
        if (!rename_file(old_root + "/" + rel_rel, new_root + "/" + rel_rel)) {
          break;
        }
      }
    }
    _creator_frame.RefreshDirectory();
    _creator_frame.MakeDirty(true);
    RefreshOurData();
  });

  _button_refresh->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [&](wxCommandEvent&) {
    _creator_frame.RefreshDirectory();
    RefreshOurData();
  });

  _button_next_unused->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [&](wxCommandEvent&) {
    auto paths = SortedPaths(*_complete_theme);
    if (paths.empty()) {
      return;
    }
    auto it = std::lower_bound(paths.begin(), paths.end(), _path_selected);
    std::size_t index = it == paths.end() ? 0 : std::distance(paths.begin(), it);
    for (std::size_t i = (index + 1) % paths.size(); i != index; i = (i + 1) % paths.size()) {
      if (!_tree_lookup.count(paths[i])) {
        continue;
      }
      if (IsUnused(session, paths[i])) {
        _tree->Select(_tree_lookup[paths[i]]);
        _tree->EnsureVisible(_tree_lookup[paths[i]]);
        RefreshTree(_tree_lookup[paths[i]]);
        break;
      }
    }
  });

  _button_random_unused->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [&](wxCommandEvent&) {
    auto paths = SortedPaths(*_complete_theme);
    if (paths.empty()) {
      return;
    }
    std::size_t unused_count = 0;
    for (std::size_t i = 0; i < paths.size(); ++i) {
      if (!_tree_lookup.count(paths[i])) {
        continue;
      }
      if (IsUnused(session, paths[i])) {
        ++unused_count;
      }
    }
    if (!unused_count) {
      return;
    }
    std::uniform_int_distribution<std::size_t> d{0, unused_count - 1};
    auto r = d(engine);
    unused_count = 0;
    for (std::size_t i = 0; i < paths.size(); ++i) {
      if (!_tree_lookup.count(paths[i])) {
        continue;
      }
      if (!IsUnused(session, paths[i])) {
        continue;
      }
      if (r < ++unused_count) {
        _tree->Select(_tree_lookup[paths[i]]);
        _tree->EnsureVisible(_tree_lookup[paths[i]]);
        RefreshTree(_tree_lookup[paths[i]]);
        break;
      }
    }
  });

  _button_next_theme->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [&](wxCommandEvent&) {
    auto theme_it = _session.mutable_theme_map()->find(_item_selected);
    if (theme_it == _session.mutable_theme_map()->end()) {
      return;
    }
    std::vector<std::string> paths;
    for (const auto& path : _complete_theme->image_path()) {
      paths.push_back(path);
    }
    for (const auto& path : _complete_theme->animation_path()) {
      paths.push_back(path);
    }
    for (const auto& path : _complete_theme->font_path()) {
      paths.push_back(path);
    }
    if (paths.empty()) {
      return;
    }
    std::sort(paths.begin(), paths.end());
    auto it = std::lower_bound(paths.begin(), paths.end(), _path_selected);
    std::size_t index = it == paths.end() ? 0 : std::distance(paths.begin(), it);
    for (std::size_t i = (index + 1) % paths.size(); i != index; i = (i + 1) % paths.size()) {
      if (!_tree_lookup.count(paths[i])) {
        continue;
      }
      const auto& t = theme_it->second;
      if (std::find(t.font_path().begin(), t.font_path().end(), paths[i]) != t.font_path().end() ||
          std::find(t.image_path().begin(), t.image_path().end(), paths[i]) !=
              t.image_path().end() ||
          std::find(t.animation_path().begin(), t.animation_path().end(), paths[i]) !=
              t.animation_path().end()) {
        _tree->Select(_tree_lookup[paths[i]]);
        _tree->EnsureVisible(_tree_lookup[paths[i]]);
        RefreshTree(_tree_lookup[paths[i]]);
        break;
      }
    }
  });

  _button_show_all->Bind(wxEVT_RADIOBUTTON, [&](wxCommandEvent&) {
    _creator_frame.RefreshDirectory();
    RefreshOurData();
  });

  _button_show_images->Bind(wxEVT_RADIOBUTTON, [&](wxCommandEvent&) {
    _creator_frame.RefreshDirectory();
    RefreshOurData();
  });

  _button_show_animations->Bind(wxEVT_RADIOBUTTON, [&](wxCommandEvent&) {
    _creator_frame.RefreshDirectory();
    RefreshOurData();
  });

  _tree->Bind(wxEVT_TREELIST_SELECTION_CHANGED,
              [&](wxTreeListEvent& e) { RefreshTree(e.GetItem()); }, wxID_ANY);

  _tree->Bind(
      wxEVT_TREELIST_ITEM_CHECKED,
      [&](wxTreeListEvent& e) {
        if (!HandleCheck(e.GetItem())) {
           e.Veto();
        }
      },
      wxID_ANY);

  _item_list->AddOnDoubleClick([&](const std::string&) {
    auto it = _tree_lookup.find(_path_selected);
    if (it == _tree_lookup.end()) {
      return;
    }
    auto checked = _tree->GetCheckedState(it->second);
    checked = checked == wxCHK_UNCHECKED ? wxCHK_CHECKED : wxCHK_UNCHECKED;
    _tree->CheckItem(it->second, checked);
    HandleCheck(it->second);
  });
}

ThemePage::~ThemePage()
{
}

void ThemePage::RefreshOurData()
{
  for (auto item = _tree->GetFirstItem(); item.IsOk(); item = _tree->GetNextItem(item)) {
    _tree->CheckItem(item, wxCHK_UNCHECKED);
  }

  auto select = [&](const std::string& s) {
    auto it = _tree_lookup.find(s);
    if (it != _tree_lookup.end()) {
      _tree->CheckItem(it->second);
      _tree->UpdateItemParentStateRecursively(it->second);
    }
  };

  int selected_text = 0;
  for (int i = 0; i < _text_list->GetItemCount(); ++i) {
    if (_text_list->GetItemState(i, wxLIST_STATE_SELECTED)) {
      selected_text = i;
      break;
    }
  }

  auto it = _session.theme_map().find(_item_selected);
  if (it != _session.theme_map().end()) {
    for (const auto& path : it->second.image_path()) {
      select(path);
    }
    for (const auto& path : it->second.animation_path()) {
      select(path);
    }
    for (const auto& path : it->second.font_path()) {
      select(path);
    }

    while (_text_list->GetItemCount() < it->second.text_line_size()) {
      _text_list->InsertItem(_text_list->GetItemCount(), "");
    }
    while (_text_list->GetItemCount() > it->second.text_line_size()) {
      _text_list->DeleteItem(_text_list->GetItemCount() - 1);
    }
    for (int i = 0; i < it->second.text_line_size(); ++i) {
      _text_list->SetItemText((long) i, NlToUser(it->second.text_line(i)));
    }
    if (it->second.text_line_size()) {
      _text_list->SetItemState(std::min(selected_text, it->second.text_line_size() - 1),
                               wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }
  } else {
    _text_list->DeleteAllItems();
  }
  _button_new->Enable(!_item_selected.empty());
  _button_edit->Enable(!_item_selected.empty() && it->second.text_line_size());
  _button_delete->Enable(!_item_selected.empty() && it->second.text_line_size());

  if (!_path_selected.empty() && _tree_lookup.find(_path_selected) != _tree_lookup.end()) {
    RefreshTree(_tree_lookup[_path_selected]);
  }
}

void ThemePage::RefreshData()
{
  _item_list->RefreshData();
  RefreshOurData();
}

void ThemePage::RefreshDirectory(const std::string& directory)
{
  *_complete_theme = trance_pb::Theme{};
  search_resources(*_complete_theme, directory);

  std::unordered_set<std::string> expanded_items;
  for (const auto& pair : _tree_lookup) {
    bool expanded = true;
    for (auto parent = pair.second; parent != _tree->GetRootItem();
         parent = _tree->GetItemParent(parent)) {
      if (!_tree->IsExpanded(parent)) {
        expanded = false;
        break;
      }
    }
    if (expanded && _tree->GetFirstChild(pair.second).IsOk()) {
      expanded_items.emplace(pair.first);
    }
  }

  _tree->DeleteAllItems();
  _tree_lookup.clear();
  _tree_lookup["."] = _tree->GetRootItem();

  std::vector<std::string> paths;
  for (const auto& path : _complete_theme->image_path()) {
    paths.push_back(path);
  }
  for (const auto& path : _complete_theme->animation_path()) {
    paths.push_back(path);
  }
  for (const auto& path : _complete_theme->font_path()) {
    paths.push_back(path);
  }
  std::sort(paths.begin(), paths.end());

  std::unordered_set<std::string> used_paths;
  for (const auto& pair : _session.theme_map()) {
    for (const auto& path : pair.second.image_path()) {
      used_paths.emplace(path);
    }
    for (const auto& path : pair.second.animation_path()) {
      used_paths.emplace(path);
    }
    for (const auto& path : pair.second.font_path()) {
      used_paths.emplace(path);
    }
  }

  for (auto it = paths.begin(); it != paths.end();) {
    if ((_button_show_images->GetValue() && !is_image(*it)) ||
        (_button_show_animations->GetValue() && !is_animation(*it))) {
      it = paths.erase(it);
    } else {
      ++it;
    }
  }

  std::size_t file_count = 0;
  std::size_t unused_count = 0;
  for (const auto& path_str : paths) {
    std::filesystem::path path{path_str};
    for (auto it = ++path.begin(); it != path.end(); ++it) {
      std::filesystem::path component;
      std::filesystem::path parent;
      for (auto jt = path.begin(); jt != it; ++jt) {
        component /= *jt;
        parent /= *jt;
      }
      component /= *it;

      if (_tree_lookup.find(component.string()) == _tree_lookup.end()) {
        wxClientData* data = nullptr;
        if (it == --path.end()) {
          ++file_count;
          data = new wxStringClientData{component.string()};
        }
        auto is_used = it != --path.end() || used_paths.count(component.string());
        auto str = is_used ? it->string() : "[UNUSED] " + it->string();
        auto item = _tree->AppendItem(_tree_lookup[parent.string()], str, -1, -1, data);
        if (!is_used) {
          ++unused_count;
        }
        _tree_lookup[component.string()] = item;
      }
    }
  }

  const auto& c = *_complete_theme;
  std::set<std::string> image_set{c.image_path().begin(), c.image_path().end()};
  std::set<std::string> animation_set{c.animation_path().begin(), c.animation_path().end()};
  std::set<std::string> font_set{c.font_path().begin(), c.font_path().end()};
  for (auto& pair : *_session.mutable_theme_map()) {
    for (auto it = pair.second.mutable_image_path()->begin();
         it != pair.second.mutable_image_path()->end();) {
      it = image_set.count(*it) ? 1 + it : pair.second.mutable_image_path()->erase(it);
    }
    for (auto it = pair.second.mutable_animation_path()->begin();
         it != pair.second.mutable_animation_path()->end();) {
      it = animation_set.count(*it) ? 1 + it : pair.second.mutable_animation_path()->erase(it);
    }
    for (auto it = pair.second.mutable_font_path()->begin();
         it != pair.second.mutable_font_path()->end();) {
      it = font_set.count(*it) ? 1 + it : pair.second.mutable_font_path()->erase(it);
    }
  }

  for (const auto& pair : _tree_lookup) {
    if (expanded_items.find(pair.first) != expanded_items.end()) {
      _tree->Expand(pair.second);
    }
  }
  _creator_frame.SetStatusText(
      "Scanned " + std::to_string(file_count) + " files in " + directory +
      " (" + std::to_string(unused_count) + " unused)");

  auto it = _tree_lookup.find(_path_selected);
  if (it == _tree_lookup.end()) {
    _path_selected.clear();
  } else {
    _tree->Select(it->second);
    _tree->EnsureVisible(it->second);
  }
}

void ThemePage::Shutdown()
{
  _image_panel->Shutdown();
}

bool ThemePage::HandleCheck(wxTreeListItem item)
{
  auto it = _session.mutable_theme_map()->find(_item_selected);
  if (it == _session.mutable_theme_map()->end()) {
    return false;
  }
  auto checked = _tree->GetCheckedState(item);
  _tree->CheckItemRecursively(item, checked);
  _tree->UpdateItemParentStateRecursively(item);

  auto handle = [&](const google::protobuf::RepeatedPtrField<std::string>& c,
                    google::protobuf::RepeatedPtrField<std::string>& t,
                    const std::string& path) {
    if (std::find(c.begin(), c.end(), path) == c.end()) {
      return;
    }
    auto it = std::find(t.begin(), t.end(), path);
    if (checked == wxCHK_CHECKED && it == t.end()) {
      *t.Add() = path;
    }
    if (checked == wxCHK_UNCHECKED && it != t.end()) {
      t.erase(it);
    }
  };

  std::function<void(const wxTreeListItem&)> recurse = [&](const wxTreeListItem& item) {
    for (auto c = _tree->GetFirstChild(item); c.IsOk(); c = _tree->GetNextSibling(c)) {
      recurse(c);
    }
    auto data = _tree->GetItemData(item);
    if (data != nullptr) {
      std::string path = ((const wxStringClientData*) data)->GetData().ToStdString();
      handle(_complete_theme->image_path(), *it->second.mutable_image_path(), path);
      handle(_complete_theme->animation_path(), *it->second.mutable_animation_path(), path);
      handle(_complete_theme->font_path(), *it->second.mutable_font_path(), path);
    }
  };
  recurse(item);
  _creator_frame.MakeDirty(true);
  RefreshHighlights();
  return true;
}

void ThemePage::RefreshTree(wxTreeListItem item)
{
  auto data = _tree->GetItemData(item);
  if (data != nullptr) {
    std::string path = ((const wxStringClientData*) data)->GetData().ToStdString();
    auto root = std::filesystem::path{_session_path}.parent_path().string();
    const auto& images = _complete_theme->image_path();
    const auto& anims = _complete_theme->animation_path();
    const auto& fonts = _complete_theme->font_path();
    if (_path_selected != path) {
      auto full_path = root + "/" + path;
      if (std::find(images.begin(), images.end(), path) != images.end()) {
        _current_font.clear();
        _image_panel->SetImage(load_image(full_path));
        _image_panel->SetInfo(GetFileSize(full_path));
      }
      if (std::find(anims.begin(), anims.end(), path) != anims.end()) {
        _current_font.clear();
        _image_panel->SetAnimation(load_animation(full_path));
        _image_panel->SetInfo(GetFileSize(full_path));
      }
      if (std::find(fonts.begin(), fonts.end(), path) != fonts.end()) {
        _current_font = full_path;
        GenerateFontPreview();
        _image_panel->SetInfo("");
      }
      _path_selected = path;
    }
  }
  RefreshHighlights();
  _button_open->Enable(true);
  _button_rename->Enable(true);
}

void ThemePage::RefreshHighlights()
{
  _item_list->ClearHighlights();
  for (const auto& pair : _session.theme_map()) {
    auto used = [&](const google::protobuf::RepeatedPtrField<std::string>& f) {
      return std::find(f.begin(), f.end(), _path_selected) != f.end();
    };
    const auto& theme = pair.second;
    if (used(theme.image_path()) || used(theme.font_path()) || used(theme.animation_path())) {
      _item_list->AddHighlight(pair.first);
    }
  }
}

void ThemePage::GenerateFontPreview()
{
  if (_current_font.empty()) {
    return;
  }
  std::string text;
  if (_current_text_line.empty()) {
    text = (--std::filesystem::path{_current_font}.end())->string();
  } else {
    text = _current_text_line;
  }

  static const unsigned border = 32;
  static const unsigned font_size = 128;
  // Render in another thread to avoid interfering with OpenGL contexts.
  std::thread worker([&] {
    sf::Font font;
    font.loadFromFile(_current_font);
    sf::Text text_obj{text, font, font_size};
    auto bounds = text_obj.getLocalBounds();

    sf::RenderTexture texture;
    texture.create(border + (unsigned) bounds.width, border + (unsigned) bounds.height);
    sf::Transform transform;
    transform.translate(border / 2 - bounds.left, border / 2 - bounds.top);
    texture.draw(text_obj, transform);
    texture.display();
    _image_panel->SetImage(texture.getTexture().copyToImage());
  });
  worker.join();
}