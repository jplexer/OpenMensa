#include <pebble.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifndef IMAGE_VEGAN
  #define IMAGE_VEGAN RESOURCE_ID_IMAGE_VEGAN
#endif
#ifndef IMAGE_VEGETARIAN
  #define IMAGE_VEGETARIAN RESOURCE_ID_IMAGE_VEGETARIAN
#endif
#ifndef IMAGE_POT
  #define IMAGE_POT RESOURCE_ID_IMAGE_POT
#endif

#define MAX_MENU_ITEMS 10
static int s_menu_item_count = 0;
static char *s_menu_titles[MAX_MENU_ITEMS];
static char *s_menu_subtitles[MAX_MENU_ITEMS];

#define MAX_MEALS 20
static int s_meal_count = 0;
static int s_meal_ids[MAX_MEALS];      // Internal IDs.
static char *s_meal_titles[MAX_MEALS]; // Meal names.
static char *s_meal_subtitles[MAX_MEALS]; // Prices as strings.
static GBitmap* s_meal_bitmaps[MAX_MEALS] = {NULL};

static Window *s_window;
static MenuLayer *s_menu_layer;
static Window *s_meals_window;
static MenuLayer *s_meals_menu_layer;
static Window *s_error_window = NULL;
static TextLayer *s_error_text_layer = NULL;
static Window *s_meal_info_window = NULL;
static TextLayer *s_meal_info_text_layer = NULL;

static char s_meal_info_name[128] = "";
static char s_meal_info_price[64] = "";
static char s_meal_info_allergens[256] = "";
static TextLayer *s_meal_info_name_layer = NULL;
static TextLayer *s_meal_info_price_layer = NULL;
static TextLayer *s_meal_info_allergens_layer = NULL;

static ScrollLayer *s_scroll_layer = NULL;

#ifndef HAVE_STRCASECMP
int strncasecmp(const char *s1, const char *s2, size_t n) {
  for (; n; s1++, s2++, n--) {
    int diff = tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
    if(diff != 0 || !*s1)
      return diff;
  }
  return 0;
}
#endif

#ifndef HAVE_STRCASESTR
char *strcasestr(const char *haystack, const char *needle) {
  if (!*needle)
    return (char *)haystack;
  for(; *haystack; haystack++){
    if(tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)){
      const char *h, *n;
      for(h = haystack, n = needle; *h && *n; h++, n++){
        if(tolower((unsigned char)*h) != tolower((unsigned char)*n))
          break;
      }
      if(!*n)
        return (char *)haystack;
    }
  }
  return NULL;
}
#endif

// Menu callbacks
static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 1;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_menu_item_count; // Dynamic count
}

static void menu_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  if(cell_index->row < s_menu_item_count) {
    menu_cell_basic_draw(ctx, cell_layer, s_menu_titles[cell_index->row], s_menu_subtitles[cell_index->row], NULL);
  }
}

// --- Update day menu selection callback ---
static void menu_selection_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if(cell_index->row < s_menu_item_count) {
    // When a date is selected, send a message to request the meals.
    DictionaryIterator *out_iter;
    AppMessageResult result = app_message_outbox_begin(&out_iter);
    if(result == APP_MSG_OK) {
      // Send the selected date string.
      dict_write_cstring(out_iter, MESSAGE_KEY_SELECTED_DATE, s_menu_titles[cell_index->row]);
      dict_write_end(out_iter);
      app_message_outbox_send();
    }
  }
}

// --- Meals menu callbacks ---
static uint16_t meals_menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 1;
}

static uint16_t meals_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_meal_count;
}

static void meals_menu_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  if(cell_index->row < s_meal_count) {
    GRect bounds = layer_get_bounds(cell_layer);
    
    // Draw the bitmap icon if available.
    int icon_size = 30;
    int icon_offset = 5;
    if(s_meal_bitmaps[cell_index->row] != NULL) {
      graphics_context_set_compositing_mode(ctx, GCompOpSet);
      GRect icon_bounds = GRect(icon_offset, (bounds.size.h - icon_size) / 2, icon_size, icon_size);
      graphics_draw_bitmap_in_rect(ctx, s_meal_bitmaps[cell_index->row], icon_bounds);
    }
    
    // Calculate textbox bounds (shift right if icon is drawn).
    GRect text_bounds = bounds;
    if(s_meal_bitmaps[cell_index->row] != NULL) {
      text_bounds.origin.x += icon_size + 2 * icon_offset;
      text_bounds.size.w -= icon_size + 2 * icon_offset;
    }
    
    // Use a small font and calculate its height.
    const GFont small_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    int small_font_height = graphics_text_layout_get_content_size("A", small_font, 
                                  GRect(0, 0, text_bounds.size.w, 1000),
                                  GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).h;
    text_bounds.size.h = small_font_height;
    
    // Set text color: white when highlighted, black otherwise.
    if(menu_cell_layer_is_highlighted(cell_layer)) {
      graphics_context_set_text_color(ctx, GColorWhite);
    } else {
      graphics_context_set_text_color(ctx, GColorBlack);
    }
    
    // Draw the meal title on one line.
    graphics_draw_text(ctx, s_meal_titles[cell_index->row], small_font, text_bounds,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    
    // Optional: Draw the subtitle (price) on a second line with smaller font.
    const GFont subtitle_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    int subtitle_font_height = graphics_text_layout_get_content_size("A", subtitle_font, 
                                      GRect(0, 0, text_bounds.size.w, 1000),
                                      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).h;
    GRect subtitle_bounds = text_bounds;
    subtitle_bounds.origin.y += small_font_height + 2; // adjust spacing as needed
    subtitle_bounds.size.h = subtitle_font_height;
    graphics_draw_text(ctx, s_meal_subtitles[cell_index->row], subtitle_font, subtitle_bounds,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    
  }
}

static void meals_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  if(cell_index->row < s_meal_count) {
    DictionaryIterator *out_iter;
    AppMessageResult result = app_message_outbox_begin(&out_iter);
    if(result == APP_MSG_OK) {
      // Send the selected meal id back to JS.
      dict_write_int(out_iter, MESSAGE_KEY_MEAL_ID, &s_meal_ids[cell_index->row], sizeof(int), true);
      dict_write_end(out_iter);
      app_message_outbox_send();
    } else {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Error sending meal ID: %d", (int)result);
    }
  }
}

// --- Meals window load/unload ---
static void meals_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  s_meals_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_meals_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = meals_menu_get_num_sections_callback,
    .get_num_rows = meals_menu_get_num_rows_callback,
    .draw_row = meals_menu_draw_row_callback,
    .select_click = meals_menu_select_callback,
  });
  menu_layer_set_click_config_onto_window(s_meals_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_meals_menu_layer));
}

static void meals_window_unload(Window *window) {
  menu_layer_destroy(s_meals_menu_layer);
  // Free meal data.
  for (int i = 0; i < s_meal_count; i++) {
    free(s_meal_titles[i]);
    free(s_meal_subtitles[i]);
    if (s_meal_bitmaps[i]) {
      gbitmap_destroy(s_meal_bitmaps[i]);
      s_meal_bitmaps[i] = NULL;
    }
  }
  s_meal_count = 0;
}

static void create_meals_window() {
  s_meals_window = window_create();
  window_set_window_handlers(s_meals_window, (WindowHandlers) {
    .load = meals_window_load,
    .unload = meals_window_unload
  });
}

// Minimal JSON parser to extract dates.
// This parses a JSON array like ["01.01.2023", "02.01.2023", …]
static void parse_day_list(const char *json) {
  s_menu_item_count = 0;
  const char *ptr = json;
  
  // Skip any whitespace and the leading '[' if present.
  while(*ptr && isspace((unsigned char)*ptr)) { ptr++; }
  if(*ptr == '[') { ptr++; }
  
  while(*ptr && s_menu_item_count < MAX_MENU_ITEMS) {
    const char *start = strchr(ptr, '\"');
    if(!start) break;
    start++;  // move past the opening quote
    const char *end = strchr(start, '\"');
    if(!end) break;  // malformed JSON
    
    size_t len = end - start;
    char *date_string = malloc(len + 1);
    if(date_string) {
      strncpy(date_string, start, len);
      date_string[len] = '\0';
      s_menu_titles[s_menu_item_count] = date_string;
      s_menu_item_count++;
    }
    ptr = end + 1;
  }
}

// Minimal JSON parser to extract weekday names.
// This parses a JSON array like ["Sun", "Mon", …]
static void parse_weekday_list(const char *json) {
  int index = 0;
  const char *ptr = json;
  
  // Skip any whitespace and the leading '[' if present.
  while(*ptr && isspace((unsigned char)*ptr)) { ptr++; }
  if(*ptr == '[') { ptr++; }
  
  while(*ptr && index < s_menu_item_count) {
    const char *start = strchr(ptr, '\"');
    if(!start) break;
    start++;  // move past the opening quote
    const char *end = strchr(start, '\"');
    if(!end) break;  // malformed JSON
    
    size_t len = end - start;
    char *weekday_string = malloc(len + 1);
    if(weekday_string) {
      strncpy(weekday_string, start, len);
      weekday_string[len] = '\0';
      s_menu_subtitles[index] = weekday_string;
      index++;
    }
    ptr = end + 1;
  }
}
// Helper: Parse a JSON string array (e.g. ["Foo","Bar",...]) into dst.
// Returns the number of items parsed.
static int parse_string_array(const char *json, char **dst, int max_count) {
  int count = 0;
  const char *ptr = json;
  while (*ptr && count < max_count) {
    const char *start = strchr(ptr, '\"');
    if (!start) break;
    start++;  // move past the opening quote
    const char *end = strchr(start, '\"');
    if (!end) break;  // malformed JSON
    size_t len = end - start;
    dst[count] = malloc(len + 1);
    if (dst[count]) {
      strncpy(dst[count], start, len);
      dst[count][len] = '\0';
      count++;
    }
    ptr = end + 1;
  }
  return count;
}

// Helper: Parse a JSON numeric array (e.g. [123,456,...]) into dst.
// Returns the number of items parsed.
static int parse_int_array(const char *json, int *dst, int max_count) {
  int count = 0;
  const char *ptr = json;
  while (*ptr && count < max_count) {
    // Skip non-digit characters.
    while (*ptr && !isdigit((unsigned char)*ptr) && *ptr != '-') {
      ptr++;
    }
    if (!*ptr) break;
    int value = atoi(ptr);
    dst[count] = value;
    count++;
    // Skip current number.
    while (*ptr && (isdigit((unsigned char)*ptr) || *ptr == '-')) {
      ptr++;
    }
  }
  return count;
}

// --- New: Back button handler for error window ---
static void error_window_back_handler(ClickRecognizerRef recognizer, void *context) {
  // Exit the app.
  window_stack_pop_all(true);
}

static void error_window_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_BACK, error_window_back_handler);
}

// --- Updated error window functions ---
static void error_window_unload(Window *window) {
  text_layer_destroy(s_error_text_layer);
  window_destroy(s_error_window);
  s_error_window = NULL;
}

static void show_error_window(const char *error_msg) {
  // If an error window is already displayed, remove it.
  if (s_error_window) {
    window_stack_remove(s_error_window, true);
  }
  s_error_window = window_create();

#ifdef PBL_ROUND
  // For round devices like Pebble Time Round, center the text.
  GRect bounds = layer_get_bounds(window_get_root_layer(s_error_window));
  s_error_text_layer = text_layer_create(GRect(10, bounds.size.h / 2 - 30, bounds.size.w - 20, 60));
  text_layer_set_font(s_error_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
#else
  s_error_text_layer = text_layer_create(GRect(0, 10, 144, 150));
  text_layer_set_font(s_error_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
#endif
  
  text_layer_set_text(s_error_text_layer, error_msg);
  text_layer_set_text_alignment(s_error_text_layer, GTextAlignmentCenter);
  layer_add_child(window_get_root_layer(s_error_window), text_layer_get_layer(s_error_text_layer));
  
  // Set the click configuration provider to handle back button clicks.
  window_set_click_config_provider(s_error_window, error_window_click_config_provider);
  
  window_set_window_handlers(s_error_window, (WindowHandlers) {
    .unload = error_window_unload,
  });
  
  window_stack_push(s_error_window, true);
}

// Create the meal info window that displays the info on a TextLayer.
static void meal_info_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Create a scroll layer that fills the window.
  s_scroll_layer = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(s_scroll_layer, window);
  
  // Dynamically calculate meal name text height.
  const GFont name_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GSize name_size = graphics_text_layout_get_content_size(s_meal_info_name, name_font,
                              GRect(0, 0, bounds.size.w, 1000),
                              GTextOverflowModeWordWrap, GTextAlignmentCenter);

  // Create the meal name text layer using its full required height.
  s_meal_info_name_layer = text_layer_create(GRect(0, 0, bounds.size.w, name_size.h));
  text_layer_set_font(s_meal_info_name_layer, name_font);
  text_layer_set_text_alignment(s_meal_info_name_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_meal_info_name_layer, GTextOverflowModeWordWrap);
  text_layer_set_text(s_meal_info_name_layer, s_meal_info_name);
  scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_meal_info_name_layer));
  
  // Define a fixed height for the price text layer.
  const GFont price_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  int price_height = 25; // you can adjust this value if needed
  
  // Create the price text layer below the meal name.
  s_meal_info_price_layer = text_layer_create(GRect(0, name_size.h, bounds.size.w, price_height));
  text_layer_set_font(s_meal_info_price_layer, price_font);
  text_layer_set_text_alignment(s_meal_info_price_layer, GTextAlignmentCenter);
  text_layer_set_text(s_meal_info_price_layer, s_meal_info_price);
  scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_meal_info_price_layer));
  
  // Dynamically calculate allergens text height.
  const GFont allergens_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GSize allergens_size = graphics_text_layout_get_content_size(s_meal_info_allergens, allergens_font,
                                    GRect(0, 0, bounds.size.w, 1000),
                                    GTextOverflowModeWordWrap, GTextAlignmentLeft);
  
  // Create the allergens text layer below the price.
  s_meal_info_allergens_layer = text_layer_create(GRect(0, name_size.h + price_height, bounds.size.w, allergens_size.h));
  text_layer_set_font(s_meal_info_allergens_layer, allergens_font);
  text_layer_set_text_alignment(s_meal_info_allergens_layer, GTextAlignmentLeft);
  text_layer_set_overflow_mode(s_meal_info_allergens_layer, GTextOverflowModeWordWrap);
  text_layer_set_text(s_meal_info_allergens_layer, s_meal_info_allergens);
  scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_meal_info_allergens_layer));

  // Set the scroll layer's content size.
  // Force content height to be at least one pixel taller than window height so scrolling is enabled.
  int content_height = name_size.h + price_height + allergens_size.h + 20; // Add some padding
  if (content_height < bounds.size.h + 1) {
    content_height = bounds.size.h + 1;
  }
  scroll_layer_set_content_size(s_scroll_layer, GSize(bounds.size.w, content_height));

  // Add the scroll layer to the window.
  layer_add_child(window_layer, scroll_layer_get_layer(s_scroll_layer));
}

static void meal_info_window_unload(Window *window) {
  text_layer_destroy(s_meal_info_name_layer);
  s_meal_info_name_layer = NULL;
  text_layer_destroy(s_meal_info_price_layer);
  s_meal_info_price_layer = NULL;
  text_layer_destroy(s_meal_info_allergens_layer);
  s_meal_info_allergens_layer = NULL;
  scroll_layer_destroy(s_scroll_layer);
  s_scroll_layer = NULL;
  
  window_destroy(s_meal_info_window);
  s_meal_info_window = NULL;
}

static void show_meal_info_window_separated(const char *name, const char *price, const char *notes) {
  // Copy individual fields into our global strings.
  strncpy(s_meal_info_name, name, sizeof(s_meal_info_name) - 1);
  s_meal_info_name[sizeof(s_meal_info_name) - 1] = '\0';
  
  strncpy(s_meal_info_price, price, sizeof(s_meal_info_price) - 1);
  s_meal_info_price[sizeof(s_meal_info_price) - 1] = '\0';
  
  strncpy(s_meal_info_allergens, notes, sizeof(s_meal_info_allergens) - 1);
  s_meal_info_allergens[sizeof(s_meal_info_allergens) - 1] = '\0';
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "s_meal_info_name: %s", s_meal_info_name);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "s_meal_info_price: %s", s_meal_info_price);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "s_meal_info_allergens: %s", s_meal_info_allergens);
  
  s_meal_info_window = window_create();
  if (!s_meal_info_window) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to create meal info window");
    return;
  }
  window_set_window_handlers(s_meal_info_window, (WindowHandlers) {
    .load = meal_info_window_load,
    .unload = meal_info_window_unload,
  });
  window_stack_push(s_meal_info_window, true);
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  // Check for an error message first.
  Tuple *error_tuple = dict_find(iterator, MESSAGE_KEY_ERROR_MSG);
  if (error_tuple) {
    show_error_window(error_tuple->value->cstring);
    return;
  }
  
  Tuple *day_list_tuple = dict_find(iterator, MESSAGE_KEY_DAY_LIST);
  Tuple *weekday_list_tuple = dict_find(iterator, MESSAGE_KEY_WEEKDAY_LIST);
  Tuple *meals_ids_tuple = dict_find(iterator, MESSAGE_KEY_MEALS_IDS);
  Tuple *meals_names_tuple = dict_find(iterator, MESSAGE_KEY_MEALS_NAMES);
  Tuple *meals_prices_tuple = dict_find(iterator, MESSAGE_KEY_MEALS_PRICES);
  Tuple *meal_name_tuple = dict_find(iterator, MESSAGE_KEY_MEAL_NAME);
  Tuple *meal_price_tuple = dict_find(iterator, MESSAGE_KEY_MEAL_PRICE);
  Tuple *meal_notes_tuple = dict_find(iterator, MESSAGE_KEY_MEAL_NOTES);
  
  // Use separate field messages if available.
  if (meal_name_tuple && meal_price_tuple && meal_notes_tuple) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Showing meal info window");
    show_meal_info_window_separated(meal_name_tuple->value->cstring,
                                    meal_price_tuple->value->cstring,
                                    meal_notes_tuple->value->cstring);
  }

  if (day_list_tuple) {
    // Free old day titles.
    for (int i = 0; i < s_menu_item_count; i++) {
      free(s_menu_titles[i]);
    }
    parse_day_list(day_list_tuple->value->cstring);
  }
  
  if (weekday_list_tuple) {
    for (int i = 0; i < s_menu_item_count; i++) {
      free(s_menu_subtitles[i]);
    }
    parse_weekday_list(weekday_list_tuple->value->cstring);
  }
  
  if (meals_ids_tuple && meals_names_tuple && meals_prices_tuple) {
    // Free previous meal data.
    for (int i = 0; i < s_meal_count; i++) {
      free(s_meal_titles[i]);
      free(s_meal_subtitles[i]);
      if (s_meal_bitmaps[i]) {
        gbitmap_destroy(s_meal_bitmaps[i]);
        s_meal_bitmaps[i] = NULL;
      }
    }
    int count_ids = parse_int_array(meals_ids_tuple->value->cstring, s_meal_ids, MAX_MEALS);
    int count_names = parse_string_array(meals_names_tuple->value->cstring, s_meal_titles, MAX_MEALS);
    int count_prices = parse_string_array(meals_prices_tuple->value->cstring, s_meal_subtitles, MAX_MEALS);
    
    // Use the minimum count among the arrays.
    s_meal_count = count_ids;
    if (count_names < s_meal_count) s_meal_count = count_names;
    if (count_prices < s_meal_count) s_meal_count = count_prices;
    
    // Process each meal title.
    for (int i = 0; i < s_meal_count; i++) {
      char *name = s_meal_titles[i];
      // Check for prefix ("Vegan" or "Vegetarian") to remove if present.
      if (strncasecmp(name, "vegan:", 6) == 0 || (strncasecmp(name, "vegan", 5) == 0 && (name[5]==' ' || name[5]==':'))) {
        char *p = name;
        if (strncasecmp(p, "vegan:", 6)==0) {
          p += 6;
        } else {
          p += 5;
        }
        while (*p == ' ' || *p == ':') { p++; }
        memmove(name, p, strlen(p)+1);
        s_meal_bitmaps[i] = gbitmap_create_with_resource(IMAGE_VEGAN);
      } else if (strncasecmp(name, "vegetarian:", 11) == 0 || (strncasecmp(name, "vegetarian", 10) == 0 && (name[10]==' ' || name[10]==':'))) {
        char *p = name;
        if (strncasecmp(p, "vegetarian:", 11) == 0) {
          p += 11;
        } else {
          p += 10;
        }
        while (*p == ' ' || *p == ':') { p++; }
        memmove(name, p, strlen(p)+1);
        s_meal_bitmaps[i] = gbitmap_create_with_resource(IMAGE_VEGETARIAN);
      } else if (strncasecmp(name, "vegetarisch:", 13) == 0 || (strncasecmp(name, "vegetarisch", 12) == 0 && (name[12]==' ' || name[12]==':'))) {
        char *p = name;
        if (strncasecmp(p, "vegetarisch:", 13) == 0) {
          p += 13;
        } else {
          p += 12;
        }
        while (*p == ' ' || *p == ':') { p++; }
        memmove(name, p, strlen(p)+1);
        s_meal_bitmaps[i] = gbitmap_create_with_resource(IMAGE_VEGETARIAN);
      } else if (strcasestr(name, "vegan") != NULL) {
        s_meal_bitmaps[i] = gbitmap_create_with_resource(IMAGE_VEGAN);
      } else if (strcasestr(name, "vegetarian") != NULL || strcasestr(name, "vegetarisch") != NULL) {
        s_meal_bitmaps[i] = gbitmap_create_with_resource(IMAGE_VEGETARIAN);
      } else {
        s_meal_bitmaps[i] = gbitmap_create_with_resource(IMAGE_POT);
      }
    }
    
    if (!s_meals_window) {
      create_meals_window();
    } else {
      // If meals window already exists, reload its menu data.
      if(s_meals_menu_layer != NULL) {
        menu_layer_reload_data(s_meals_menu_layer);
      }
    }
    window_stack_push(s_meals_window, true);
    window_stack_push(s_meals_window, true);
  }
  
  menu_layer_reload_data(s_menu_layer);
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_selection_callback
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void prv_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  // A message was received, but had to be dropped
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped. Reason: %d", (int)reason);
}


static void prv_init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  // Initialize AppMessage and set inbox handler
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_open(1024, 1024);
}

static void prv_deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}