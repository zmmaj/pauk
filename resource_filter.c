// resource_filter.c
#include "resource_filter.h"

// ===== MASTER BLOCK LIST =====
// Add all blocked patterns here - easy to maintain!
static const BlockedPattern blocked_patterns[] = {
    // Font services
    { "fonts.googleapis.com", "Google Fonts CSS" },
    { "fonts.gstatic.com", "Google Fonts resource" },
    { "fonts.google.com", "Google Fonts" },
    { "typekit.net", "Adobe Fonts" },
    { "cloud.typography.com", "Cloud Typography" },
    { "use.typekit.net", "Typekit" },
    { "fast.fonts.net", "Fast Fonts" },
    
    // Font file extensions
    { ".woff", "WOFF font file" },
    { ".woff2", "WOFF2 font file" },
    { ".ttf", "TTF font file" },
    { ".otf", "OTF font file" },
    { ".eot", "EOT font file" },
    { ".pfa", "PFA font file" },
    { ".pfb", "PFB font file" },
    
    // Swagger/API docs
    { "swagger-ui.css", "Swagger UI CSS" },
    { "swagger_ui.css", "Swagger UI CSS" },
    { "/flasgger_static/", "Flasgger static" },
    { "swagger", "Swagger resource" },
    
    // Google APIs (non-CSS)
    { "googleapis.com", "Google API" },
    { "googletagmanager.com", "Google Tag Manager" },
    { "google-analytics.com", "Google Analytics" },
    { "ebx.js", "ExoBrain/ad script" },    
    { "ebx", "ExoBrain tracking" },       
    { "exobrain", "ExoBrain ad network" }, 
    { "adsbygoogle.js", "Google AdSense" },       
    { "adsbygoogle", "Google AdSense" },             
    { "pagead2.googlesyndication.com", "Google AdSense" }, 
    { "googlesyndication", "Google Syndication ads" },  
    
     // ===== ADD BOOTSTRAP AND LARGE CSS FRAMEWORKS =====
     { "bootstrap.min.css", "Bootstrap CSS (too complex for V1)" },
     { "bootstrap.css", "Bootstrap CSS" },
     { "bootstrap", "Bootstrap framework" },
     { "slick.css", "Slick carousel CSS" },       
     { "slick-theme.css", "Slick carousel theme" }, 
     { "slick", "Slick carousel" }, 
     { "fontawesome", "FontAwesome icons" },
     { "font-awesome", "FontAwesome icons" },
     { "all.min.css", "Minified CSS (may cause issues)" },
     { "/assets/", "svi assets problemi" },
     { "/ado.js", "ado.js problem" },
     
       // ===== BLOCK JQUERY =====
    { "jquery.js", "jQuery library" },
    { "jquery.min.js", "jQuery minified" },
    { "jquery-", "jQuery versioned file" },  // catches jquery-3.5.1.min.js, etc.
    { "jquery.", "jQuery file" },
    { "/jquery/", "jQuery directory" },
    { "code.jquery.com", "jQuery CDN" },
    { "ajax.googleapis.com/ajax/libs/jquery", "Google jQuery CDN" },
    { "cdnjs.cloudflare.com/ajax/libs/jquery", "Cloudflare jQuery CDN" },
    { "cdn.jsdelivr.net/npm/jquery", "jsDelivr jQuery CDN" },
     
     // ===== ADD OTHER LARGE FRAMEWORKS =====
     { "tailwind", "Tailwind CSS" },
     { "foundation", "Foundation CSS" },
     { "bulma", "Bulma CSS" },
     { "semantic", "Semantic UI" },
     { "materialize", "Materialize CSS" },

    // Ad/tracker services
    { "adocean.pl", "AdOcean ad server" },
    { "brid.tv", "Brid video player" },
    { "facebook.com/tr", "Facebook tracking" },

        // ===== ANALYTICS & TRACKING SCRIPTS =====
        { "smartocto.com", "SmartOcto analytics" },   
        { "smartocto", "SmartOcto tracking" },             
        { "tentacles.smartocto.com", "SmartOcto tracker" }, 
        { "tentacles", "SmartOcto tentacles script" },     
    
    // Font-related paths (with .css)
    { "/font", "Font directory" },
    { "/fonts", "Fonts directory" },
    
    // Optional: CDN resources you want to skip
     { "cdnjs.cloudflare.com", "Cloudflare CDN" },
     { "unpkg.com", "Unpkg CDN" },
    
    { NULL, NULL }  // Sentinel
};

// Check if URL contains any blocked pattern
bool is_resource_blocked(const char *url) {
    if (!url) return true;  // No URL = block
    
    for (int i = 0; blocked_patterns[i].pattern != NULL; i++) {
        if (strstr(url, blocked_patterns[i].pattern) != NULL) {
            return true;
        }
    }
    
    return false;
}

// Get the reason why a URL is blocked (for logging)
const char* get_block_reason(const char *url) {
    if (!url) return "NULL URL";
    
    for (int i = 0; blocked_patterns[i].pattern != NULL; i++) {
        if (strstr(url, blocked_patterns[i].pattern) != NULL) {
            return blocked_patterns[i].reason;
        }
    }
    
    return "Not blocked";
}

// Optional: Print all blocked patterns (for debugging)
void print_blocked_patterns(void) {
    printf("\n=== Blocked Resource Patterns ===\n");
    for (int i = 0; blocked_patterns[i].pattern != NULL; i++) {
        printf("  %s -> %s\n", blocked_patterns[i].pattern, blocked_patterns[i].reason);
    }
    printf("================================\n");
}
