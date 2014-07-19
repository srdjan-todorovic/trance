#ifndef TRANCE_IMAGES_H
#define TRANCE_IMAGES_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <SFML/Graphics.hpp>

// In-memory image with load-on-request OpenGL texture which is ref-counted
// and automatically unloaded once no longer used.
struct Image {
  struct texture_deleter {
    texture_deleter(std::size_t texture)
      : texture(texture) {}
    ~texture_deleter();
    std::size_t texture;
  };

  Image(const std::string& path)
    : path(path)
    , width(0)
    , height(0)
    , texture(0) {}

  std::string path;
  unsigned int width;
  unsigned int height;
  std::shared_ptr<sf::Image> sf_image;
  mutable std::size_t texture;
  mutable std::shared_ptr<texture_deleter> deleter;
};

// Set of Images and associated text strings.
class ImageSet {
public:

  ImageSet(const std::vector<std::string>& paths,
           const std::vector<std::string>& texts);
  ImageSet(const ImageSet& images);

  // Get a random loaded in-memory Image or text string.
  //
  // Note: get() is called from the main rendering thread and can upload
  // images from RAM to video memory on-demand. The other loading functions
  // are called from the async_update thread and load images from files
  // into RAM when requested.
  Image get() const;
  const std::string& get_text() const;

  // Set the target number of images this set should keep in memory.
  // Once changed, the asynchronous image-loading thread will gradually
  // load/unload images until we're at the target.
  void set_target_load(std::size_t target_load);
  std::size_t get_target_load() const;

  // Randomly swap out one in-memory image for another unloaded one.
  void perform_swap();
  // Perform at most one load or unload towards the target.
  void perform_load();
  // Perform all loads/unloads to reach the target.
  void perform_all_loads();

  // How many images are actually loaded.
  bool all_loaded() const;
  std::size_t loaded() const;

private:

  bool load_internal(Image* image, const std::string& path) const;
  void load_internal();
  void unload_internal();
  
  std::vector<std::string> _paths;
  std::vector<std::string> _texts;
  std::vector<Image> _images;
  std::size_t _target_load;
  mutable std::size_t _last_id;
  mutable std::size_t _last_text_id;
  mutable std::mutex _mutex;

};

// ImageBank keeps two ImageSets active at all times with a number of images
// in memory each so that a variety of these images can be displayed with no
// load delay. It also asynchronously loads a third set into memory so that
// the active sets can be swapped out.
class ImageBank {
public:

  // Add a bunch of sets with image paths and text strings, then call
  // initialise().
  void add_set(const std::vector<std::string>& paths,
               const std::vector<std::string>& texts);
  void initialise();

  // Get Images/text strings from either of the two active sets.
  Image get(bool alternate = false) const;
  const std::string& get_text(bool alternate = false) const;

  // Call to upload a random image from the next set which has been loaded
  // into RAM but not video memory.
  
  // This has to happen on the main rendering thread since OpenGL contexts
  // are single-threaded by default, but this function call can be timed to
  // mitigate the upload cost of switching active sets.
  void maybe_upload_next();

  // If the next set has been fully loaded, swap it out for one of the two
  // active sets.
  bool change_sets();

  // Called from separate update thread to perform async loading/unloading.
  void async_update();

private:

  static const std::size_t switch_cooldown = 500;

  std::size_t _prev;
  std::size_t _a;
  std::size_t _b;
  std::size_t _next;

  std::vector<ImageSet> _sets;
  unsigned int _updates;
  unsigned int _cooldown;

};

#endif