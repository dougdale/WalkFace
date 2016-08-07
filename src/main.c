#include <pebble.h>
//#define EMULATOR

static Window *s_main_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static Layer *s_canvas_layer;

#define COLOR_LIST_SIZE 5
#define STEP_HOURS 16
#define STEP_HOURS_START 6
#define STEP_HOURS_END (STEP_HOURS_START + STEP_HOURS)
#define STEP_HISTORY_SIZE 24
#define STEP_GOAL_DAYLY 10000
#define STEP_GOAL_HOURLY 250

static GColor color_list[COLOR_LIST_SIZE];
static int step_history[STEP_HISTORY_SIZE];
static bool steps_initialized;
static time_t start_of_today;
static HealthMinuteData minute_data[MINUTES_PER_HOUR];
static int init_hour_index;

static void zero_steps() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "In zero_steps()");
  for (int i = 0; i < STEP_HISTORY_SIZE; i++)
      step_history[i] = 0;
}

static int hour_index(time_t hour, time_t start) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "In hour_index()");
  int index = (hour - start) / SECONDS_PER_HOUR;

  if (index < 0) {
    index = 0;
  } else if (index >= STEP_HISTORY_SIZE) {
    index = STEP_HISTORY_SIZE - 1;
  }

  APP_LOG(APP_LOG_LEVEL_DEBUG, "index: %d", index);

  return index;
}

static int get_steps(time_t start, time_t end) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "In get_steps()");
  int steps = 0;

  time_t run_seconds;
  uint16_t run_ms;

  // Check if data is available
  HealthServiceAccessibilityMask result =
      health_service_metric_accessible(HealthMetricStepCount, start, end);

  if(result & HealthServiceAccessibilityMaskAvailable) {
    time_ms(&run_seconds, &run_ms);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "start: %ld %d", run_seconds, run_ms);

    // Have to get minute by minute data since
    // health_service_sum() just returns a weighted average
    uint32_t num_records = health_service_get_minute_history(
      &minute_data[0], MINUTES_PER_HOUR, &start, &end);

    time_ms(&run_seconds, &run_ms);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "end: %ld %d", run_seconds, run_ms);

    // Sum minute by minute data. It's possible not all records are valid
    for (uint32_t i = 0; i < num_records; i++) {
      if (!minute_data[i].is_invalid) {
        steps += minute_data[i].steps;
      }
    }
  }

  APP_LOG(APP_LOG_LEVEL_DEBUG, "steps: %d", steps);

  return steps;
}

static void update_steps(time_t current_time) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "In update_steps()");
  time_t start = start_of_today;
  time_t hour;

  // If new day detected, zero the steps
  if (start != start_of_today) {
    start_of_today = start;
    zero_steps();
  }

  // Steps for previous hours initialized?
  if (steps_initialized) {
    // Yes, just update the current hour.
    hour = current_time - (current_time % SECONDS_PER_HOUR);

    int this_hour = hour_index(hour, start_of_today);
    if (this_hour >= STEP_HOURS_START && this_hour < STEP_HOURS_END) {
#ifndef EMULATOR
      step_history[this_hour] = get_steps(hour, current_time);
#else
      // Steps not returned in emulator. Just add to current steps.
      step_history[this_hour] += 5;
#endif
    }
  }
}

static void update_time() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "In update_time()");
  // Get a tm structure
  time_t current = time(NULL);
  struct tm *tick_time = localtime(&current);

  // Update the steps information
  update_steps(current);

  // Format the time and display
  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

  // Now format the date and display
  static char s_date_buffer[10];
  strftime(s_date_buffer, sizeof(s_date_buffer), "%a %m/%d", tick_time);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}


// Update the background reflecting steps
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "In canvas_update_proc()");
  GRect layer_bounds = layer_get_bounds(layer);
  int rh = layer_bounds.size.h / 8;
  int cw = rh;

  for (int hour_index = 0; hour_index < STEP_HOURS; hour_index++) {
    int row = hour_index % 8;
    int col = hour_index / 8;

    // Set the fill color
    int color_index =
      (step_history[hour_index + STEP_HOURS_START] * (COLOR_LIST_SIZE - 1))
      / STEP_GOAL_HOURLY;

    if (color_index >= COLOR_LIST_SIZE) {
      color_index = COLOR_LIST_SIZE - 1;
    }

    GColor color = color_list[color_index];
    graphics_context_set_fill_color(ctx, color);

    // Draw
    GRect rect_bounds = GRect(col * cw, row * rh, cw, rh);

    graphics_fill_rect(ctx, rect_bounds, 0, GCornerNone);
  }
}

static void main_window_load(Window *window) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "In main_window_load()");
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Set up the background layer
  window_set_background_color(window, GColorWhite);

  // Create canvas layer
  s_canvas_layer = layer_create(bounds);

  // Assign the custom drawing procedure
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);

  // Add to Window
  layer_add_child(window_get_root_layer(window), s_canvas_layer);

  // Set up the time layer

  // Create the TextLayer with specific bounds
  s_time_layer = text_layer_create(
      GRect(0, 0, bounds.size.w, 50));

  // Improve the layout to be more like a watchface
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentRight);

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  // Set up the date layer

  // Create the TextLayer with specific bounds
  s_date_layer = text_layer_create(
      GRect(0, 50, bounds.size.w, 50));

  // Improve the layout to be more like a watchface
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorBlack);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentRight);

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));
}

static void main_window_unload(Window *window) {
  // Destroy TextLayer
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
}

static void init_hour(void *data)
{
  time_t hour = start_of_today + (init_hour_index * SECONDS_PER_HOUR);
  time_t end = hour + SECONDS_PER_HOUR;
  time_t current = time(NULL);

  if (end > current) {
    end = current;
    steps_initialized = true;
  }

#ifndef EMULATOR
  step_history[init_hour_index++] = get_steps(hour, end);
#else
  // Health services don't return steps in emulator. Just put random
  // data in.
  step_history[init_hour_index++] = rand() % 300;
#endif

  layer_mark_dirty(s_canvas_layer);

  if (!steps_initialized) {
    app_timer_register(100, init_hour, NULL);
  }
}

static void init() {
  // Since we can't initialize at the declaration, fill
  // color_list[] here.
  color_list[0] = GColorBlack;
  color_list[1] = GColorRed;
  color_list[2] = GColorChromeYellow;
  color_list[3] = GColorYellow;
  color_list[4] = GColorGreen;

  // Set start_of_today
  start_of_today = time_start_of_today();

  // Indicate steps need to be initialized and zero steps
  zero_steps();

  if (time(NULL) > start_of_today + (STEP_HOURS_START * SECONDS_PER_HOUR)) {
      init_hour_index = STEP_HOURS_START;
      steps_initialized = false;
      app_timer_register(500, init_hour, NULL);
  } else {
      steps_initialized = true;
  }

  // Create main Window element and assign to pointer
  s_main_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);

  // Make sure the time is displayed from the start
  update_time();

  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit() {
  // Destroy Window
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
