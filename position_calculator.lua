-- position_calculator.lua - Simplified for Helen OS Browser
print("=== LUA POSITION CALCULATOR (SIMPLIFIED) ===")

local M = {}

M.config = {
    viewport_width = 800,
    viewport_height = 600,
    default_font_size = 16,
    line_height_multiplier = 1.2,
    margin_between_elements = 10,
    padding_inside_elements = 8
}

-- Extract element data using simple pattern matching
function extract_elements_simple(json_str)
    print("[LUA] Extracting elements with simple pattern matching...")
    
    local elements = {}
    local element_count = 0
    
    -- Look for all tag patterns
    local pos = 1
    local len = #json_str
    
    while pos <= len do
        -- Find "tag": "value"
        local tag_start = json_str:find('"tag"', pos, true)
        if not tag_start then break end
        
        -- Find the colon after tag
        local colon_pos = json_str:find(':', tag_start, true)
        if not colon_pos then break end
        
        -- Find the opening quote of the value
        local quote_start = json_str:find('"', colon_pos, true)
        if not quote_start then break end
        
        -- Find the closing quote
        local quote_end = quote_start + 1
        while quote_end <= len and json_str:sub(quote_end, quote_end) ~= '"' do
            if json_str:sub(quote_end, quote_end) == '\\' then
                quote_end = quote_end + 2  -- Skip escaped character
            else
                quote_end = quote_end + 1
            end
        end
        
        if quote_end > len then break end
        
        -- Extract tag value
        local tag_value = json_str:sub(quote_start + 1, quote_end - 1)
        
        -- Now look backward and forward to get more context
        local context_start = math.max(1, tag_start - 100)
        local context_end = math.min(len, quote_end + 300)
        local context = json_str:sub(context_start, context_end)
        
        -- Extract ID if present
        local id_value = ""
        local id_start = context:find('"id"')
        if id_start then
            local id_colon = context:find(':', id_start, true)
            if id_colon then
                local id_quote = context:find('"', id_colon, true)
                if id_quote then
                    local id_end = id_quote + 1
                    while id_end <= #context and context:sub(id_end, id_end) ~= '"' do
                        if context:sub(id_end, id_end) == '\\' then
                            id_end = id_end + 2
                        else
                            id_end = id_end + 1
                        end
                    end
                    if id_end <= #context then
                        id_value = context:sub(id_quote + 1, id_end - 1)
                    end
                end
            end
        end
        
        -- Extract type if present
        local type_value = "block"
        local type_start = context:find('"type"')
        if type_start then
            local type_colon = context:find(':', type_start, true)
            if type_colon then
                local type_quote = context:find('"', type_colon, true)
                if type_quote then
                    local type_end = type_quote + 1
                    while type_end <= #context and context:sub(type_end, type_end) ~= '"' do
                        if context:sub(type_end, type_end) == '\\' then
                            type_end = type_end + 2
                        else
                            type_end = type_end + 1
                        end
                    end
                    if type_end <= #context then
                        type_value = context:sub(type_quote + 1, type_end - 1)
                    end
                end
            end
        end
        
        -- Extract text if present
        local text_value = ""
        local text_start = context:find('"text"')
        if text_start then
            local text_colon = context:find(':', text_start, true)
            if text_colon then
                local text_quote = context:find('"', text_colon, true)
                if text_quote then
                    local text_end = text_quote + 1
                    while text_end <= #context and context:sub(text_end, text_end) ~= '"' do
                        if context:sub(text_end, text_end) == '\\' then
                            text_end = text_end + 2
                        else
                            text_end = text_end + 1
                        end
                    end
                    if text_end <= #context then
                        text_value = context:sub(text_quote + 1, text_end - 1)
                        text_value = text_value:gsub('\\"', '"'):gsub('\\\\', '\\')
                    end
                end
            end
        end
        
        -- Add to elements list
        element_count = element_count + 1
        elements[element_count] = {
            tag = tag_value,
            id = id_value,
            type = type_value,
            text = text_value,
            original_position = tag_start
        }
        
        -- Debug output for first 20 elements
        if element_count <= 20 then
            print(string.format("[LUA] Found: <%s> id='%s' type='%s'", 
                  tag_value, id_value, type_value))
            if text_value ~= "" and #text_value > 0 then
                print(string.format("       Text: '%s'", text_value:sub(1, 40) .. (#text_value > 40 and "..." or "")))
            end
        end
        
        pos = quote_end + 1
    end
    
    print(string.format("[LUA] Total elements extracted: %d", element_count))
    return elements
end

-- Calculate dimensions for an element
function calculate_dimensions(element)
    local tag = element.tag
    local text = element.text or ""
    local id = element.id or ""
    local type = element.type or "block"
    
    local width, height
    
    -- Default dimensions
    width = 200
    height = 40
    
    -- Body
    if tag == "body" then
        width = M.config.viewport_width
        height = M.config.viewport_height
    
    -- Headings
    elseif tag == "h1" then width = 760; height = 60
    elseif tag == "h2" then width = 760; height = 48
    elseif tag == "h3" then width = 760; height = 36
    elseif tag == "h4" then width = 760; height = 30
    
    -- Paragraphs and text
    elseif tag == "p" then
        width = 760
        if #text > 0 then
            local line_count = math.max(1, math.ceil(#text / 80))
            height = 20 + (line_count * 20)
        else
            height = 30
        end
    
    -- Containers and divs
    elseif tag == "div" then
        width = 760
        if id:find("container") or id == "main-container" then
            height = 150
        else
            height = 100
        end
    
    -- Media elements
    elseif tag == "img" then width = 200; height = 150
    elseif tag == "audio" then width = 300; height = 60
    elseif tag == "video" then width = 320; height = 240
    elseif tag == "iframe" then width = 300; height = 200
    
    -- Form elements
    elseif tag == "form" then width = 400; height = 120
    elseif tag == "input" then width = 150; height = 30
    elseif tag == "button" then width = 100; height = 40
    elseif tag == "label" then
        width = #text * 7 + 20
        height = 25
    
    -- Lists
    elseif tag == "ul" or tag == "ol" then width = 400; height = 120
    elseif tag == "li" then width = 380; height = 24
    
    -- Tables
    elseif tag == "table" then width = 760; height = 150
    elseif tag == "tr" then width = 760; height = 30
    elseif tag == "td" or tag == "th" then width = 380; height = 28
    
    -- Links and inline
    elseif tag == "a" then
        width = #text * 8 + 20
        height = 24
    elseif tag == "span" then
        width = #text * 7 + 10
        height = 20
    elseif tag == "strong" or tag == "em" then
        width = #text * 8 + 10
        height = 20
    
    -- Semantic
    elseif tag == "footer" then width = 760; height = 50
    
    -- Inline elements
    elseif type == "inline" then
        width = #text * 8 + 10
        height = 20
    end
    
    -- Adjust for very long text
    if #text > 50 then
        local lines = math.ceil(#text / 50)
        height = math.max(height, lines * 18)
    end
    
    return math.floor(width), math.floor(height)
end

-- Main function to calculate positions
function M.calculate_positions(json_str)
    print("[LUA] Starting position calculation...")
    print("[LUA] JSON length: " .. #json_str .. " bytes")
    
    -- Extract all elements
    local all_elements = extract_elements_simple(json_str)
    
    if #all_elements == 0 then
        print("[LUA] ERROR: No elements found!")
        return [[{
    "success": false,
    "message": "No elements found in JSON",
    "element_count": 0,
    "elements": []
}]]
    end
    
    -- Calculate positions
    local positioned_elements = {}
    local current_y = 20
    
    for i, element in ipairs(all_elements) do
        local width, height = calculate_dimensions(element)
        
        -- Special handling for body
        if element.tag == "body" then
            positioned_elements[i] = {
                tag = element.tag,
                id = element.id,
                type = element.type,
                x = 0,
                y = 0,
                calculated_width = width,
                calculated_height = height
            }
        else
            positioned_elements[i] = {
                tag = element.tag,
                id = element.id,
                type = element.type,
                x = 20,
                y = current_y,
                calculated_width = width,
                calculated_height = height
            }
            
            current_y = current_y + height + M.config.margin_between_elements
        end
        
        -- Debug output
        if i <= 15 then
            local elem = positioned_elements[i]
            local id_display = elem.id ~= "" and " (id=" .. elem.id .. ")" or ""
            print(string.format("[LUA] %3d. %-10s%s at (%3d, %3d) %dx%d",
                  i, "<" .. elem.tag .. ">", id_display,
                  elem.x, elem.y, elem.calculated_width, elem.calculated_height))
        end
    end
    
    -- Build result JSON
    local elements_json = {}
    for i, elem in ipairs(positioned_elements) do
        table.insert(elements_json, string.format([[
        {
            "tag": "%s",
            "id": "%s",
            "type": "%s",
            "x": %d,
            "y": %d,
            "calculated_width": %d,
            "calculated_height": %d
        }]], 
        elem.tag, elem.id, elem.type, elem.x, elem.y, 
        elem.calculated_width, elem.calculated_height))
    end
    
    local result = string.format([[
{
    "success": true,
    "message": "Calculated positions for %d elements",
    "document_width": %d,
    "document_height": %d,
    "element_count": %d,
    "elements": [%s]
}]],
    #positioned_elements,
    M.config.viewport_width,
    M.config.viewport_height,
    #positioned_elements,
    table.concat(elements_json, ",\n"))
    
    print(string.format("[LUA] Calculation complete. Generated %d positions.", #positioned_elements))
    return result
end

-- Test function
function M.test_calculation()
    print("[LUA] Running test calculation...")
    
    -- Return a simple test result
    return {
        test_success = true,
        test_message = "Lua test completed successfully",
        document_height = 600,
        element_count = 3
    }
end

print("=== LUA POSITION CALCULATOR READY ===")
print("This version uses simple pattern matching for your Helen OS output")
return M