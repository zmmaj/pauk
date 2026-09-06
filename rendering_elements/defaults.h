// rendering_elements/defaults.h
#ifndef DEFAULTS_H
#define DEFAULTS_H

#include "cjson.h"
//#include "media_processor.h"

// ========== COMPLETE DEFAULTS ==========

// Basic properties
#define DEFAULT_TYPE "block"
#define DEFAULT_DISPLAY "block"
#define DEFAULT_POSITION "static"
#define DEFAULT_VISIBILITY "visible"
#define DEFAULT_TAG ""
#define DEFAULT_TABLE_FILE ""
#define DEFAULT_FORM_FILE ""
#define DEFAULT_LIST_FILE ""
#define DEFAULT_MENU_FILE ""

// Text properties
#define DEFAULT_TEXT ""
#define DEFAULT_FONT_FAMILY "DejaVuSans"
#define DEFAULT_FONT_SIZE 16
#define DEFAULT_FONT_STYLE "normal"
#define DEFAULT_FONT_WEIGHT "normal"
#define DEFAULT_TEXT_ALIGN "left"
#define DEFAULT_LIST_STYLE_TYPE "disc"
#define DEFAULT_TEXT_VERTICAL_ALIGN "baseline"
#define DEFAULT_TEXT_DECORATION "none"
#define DEFAULT_TEXT_TRANSFORM "none"

// Color properties
#define DEFAULT_COLOR "#000000"
#define DEFAULT_BG_COLOR "#FFFFFF"

// Layout properties
#define LAYOUT_NOT_CALCULATED -99999
#define DEFAULT_X LAYOUT_NOT_CALCULATED
#define DEFAULT_Y LAYOUT_NOT_CALCULATED
#define DEFAULT_WIDTH "auto"
#define DEFAULT_HEIGHT "auto"
#define DEFAULT_Z_INDEX 0

// Margin defaults
#define DEFAULT_MARGIN_TOP 0
#define DEFAULT_MARGIN_RIGHT 0
#define DEFAULT_MARGIN_BOTTOM 0
#define DEFAULT_MARGIN_LEFT 0

// Padding defaults
#define DEFAULT_PADDING_TOP 0
#define DEFAULT_PADDING_RIGHT 0
#define DEFAULT_PADDING_BOTTOM 0
#define DEFAULT_PADDING_LEFT 0

// Border defaults
#define DEFAULT_BORDER_COLOR "#000000"
#define DEFAULT_BORDER_WIDTH 0
#define DEFAULT_BORDER_STYLE "none"
#define DEFAULT_BORDER_RADIUS 0

// Content properties
#define DEFAULT_SRC ""
#define DEFAULT_HREF ""
#define DEFAULT_ALT ""
#define DEFAULT_TITLE ""
#define DEFAULT_CONTENT ""

// Data attributes
#define DEFAULT_DATA_ATTRIBUTE_COUNT 0

// Boolean flags (all false)
#define DEFAULT_IS_TABLE 0
#define DEFAULT_IS_FORM 0
#define DEFAULT_IS_MENU 0
#define DEFAULT_IS_IMAGE 0
#define DEFAULT_IS_LINK 0
#define DEFAULT_IS_BUTTON 0
#define DEFAULT_IS_INPUT 0
#define DEFAULT_IS_LIST 0
#define DEFAULT_IS_LIST_ITEM 0
#define DEFAULT_IS_HEADING 0
#define DEFAULT_IS_PARAGRAPH 0
#define DEFAULT_IS_INLINE 0
#define DEFAULT_IS_BLOCK 0
#define DEFAULT_HAS_LAYOUT 0
#define DEFAULT_HAS_FOCUS 0
#define DEFAULT_IS_CLICKABLE 0
#define DEFAULT_IS_EDITABLE 0
#define DEFAULT_IS_SELECTABLE 0

// Table flags
#define DEFAULT_TABLE_FILE ""
#define DEFAULT_FORM_FILE ""
#define DEFAULT_LIST_FILE ""
#define DEFAULT_MENU_FILE ""
#define DEFAULT_SRC ""

// Semantic flags
#define DEFAULT_IS_SEMANTIC 0
#define DEFAULT_IS_HEADER 0
#define DEFAULT_IS_FOOTER 0
#define DEFAULT_IS_SECTION 0
#define DEFAULT_IS_ARTICLE 0
#define DEFAULT_IS_ASIDE 0
#define DEFAULT_IS_MAIN 0
#define DEFAULT_IS_NAV 0
#define DEFAULT_IS_FIGURE 0
#define DEFAULT_IS_FIGCAPTION 0
#define DEFAULT_IS_TIME 0
#define DEFAULT_IS_MARK 0
#define DEFAULT_IS_SUMMARY 0
#define DEFAULT_IS_DETAILS 0
#define DEFAULT_IS_DIALOG 0
#define DEFAULT_IS_METER 0
#define DEFAULT_IS_PROGRESS 0
#define DEFAULT_IS_OUTPUT 0
#define DEFAULT_IS_DATA 0

// Media flags
#define DEFAULT_IS_MEDIA 0
#define DEFAULT_MEDIA_TYPE ""
#define DEFAULT_IS_AUDIO 0
#define DEFAULT_IS_VIDEO 0
#define DEFAULT_IS_CANVAS 0
#define DEFAULT_IS_IFRAME 0

// IFRAME defaults
#define DEFAULT_FULLY_SANDBOXED 0
#define DEFAULT_ALLOWFULLSCREEN 0
#define DEFAULT_ALLOWPAYMENTREQUEST 0

// Details defaults
#define DEFAULT_DETAILS_OPEN 0
#define DEFAULT_IS_EXPANDED 0
#define DEFAULT_HAS_DEFAULT_SUMMARY 0

// Element ID and classes
#define DEFAULT_ID ""
#define DEFAULT_CLASS_STRING ""

// Hierarchy defaults
#define DEFAULT_ELEMENT_ID -1
#define DEFAULT_PARENT_ID -1


// ========== LAYOUT SPACING DEFAULTS ==========
#define DEFAULT_ELEMENT_SPACING 8      // 8px between sibling elements
#define DEFAULT_PARENT_INDENT 8       // 8px children indent from parent
#define DEFAULT_CONTAINER_PADDING 15   // 12px padding inside containers
#define DEFAULT_BLOCK_STACK_SPACING 20 // Space between block elements
#define DEFAULT_INLINE_SPACING 2      // Space between inline elements
// ========== FUNCTION PROTOTYPES ==========

// Initialize ALL defaults in a cJSON object
void init_json_with_all_defaults(cJSON *json, const char *tag);

// Set element-specific defaults after basic init
void set_element_specific_defaults(cJSON *json, const char *tag);

// Get default value for a specific property
const char* get_default_string(const char *property_name);
int get_default_number(const char *property_name);
int get_default_bool(const char *property_name);

// ========== FUNCTION TO GET SPACING ==========
int get_spacing_between_elements(const char *parent_tag, const char *child_tag, 
    const char *parent_type, const char *child_type);

#endif // DEFAULTS_H
