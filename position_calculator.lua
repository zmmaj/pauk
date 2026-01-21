-- position_calculator.lua - FIXED VERSION with hierarchical positioning
-- Fixed nested element X positioning and container awareness
print("=== LUA POSITION CALCULATOR (FIXED HIERARCHICAL) ===")

local M = {}

M.config = {
    viewport_width = 800,
    viewport_height = 600,
    default_font_size = 16,
    line_height_multiplier = 1.2,
    margin_between_elements = 15,
    padding_inside_elements = 8,
    container_indent = 40,
    regular_indent = 20
}

-- Simple JSON tokenizer for your specific format
function tokenize_json(json_str)
    local tokens = {}
    local pos = 1
    local len = #json_str
    
    while pos <= len do
        local char = json_str:sub(pos, pos)
        
        if char == '"' then
            -- String token
            local start = pos
            pos = pos + 1
            while pos <= len do
                if json_str:sub(pos, pos) == '"' and json_str:sub(pos-1, pos-1) ~= '\\' then
                    break
                end
                pos = pos + 1
            end
            pos = pos + 1
            table.insert(tokens, {type="string", value=json_str:sub(start, pos-1)})
            
        elseif char == '{' or char == '}' or char == '[' or char == ']' or char == ':' or char == ',' then
            -- Single character token
            table.insert(tokens, {type="char", value=char})
            pos = pos + 1
            
        elseif char:match("%s") then
            -- Whitespace, skip
            pos = pos + 1
            
        elseif char:match("%d") or char == '-' then
            -- Number
            local start = pos
            while pos <= len and (json_str:sub(pos, pos):match("%d") or json_str:sub(pos, pos) == '.' or json_str:sub(pos, pos) == '-') do
                pos = pos + 1
            end
            table.insert(tokens, {type="number", value=json_str:sub(start, pos-1)})
            
        elseif char:match("%a") then
            -- Literal (true, false, null)
            local start = pos
            while pos <= len and json_str:sub(pos, pos):match("%a") do
                pos = pos + 1
            end
            table.insert(tokens, {type="literal", value=json_str:sub(start, pos-1)})
            
        else
            -- Unknown, skip
            pos = pos + 1
        end
    end
    
    return tokens
end

-- Extract elements from token stream
function extract_elements_from_tokens(tokens)
    local elements = {}
    local stack = {}
    local current_obj = nil
    local current_key = nil
    local in_array = false
    
    for i, token in ipairs(tokens) do
        if token.type == "char" and token.value == "{" then
            -- Start new object
            table.insert(stack, {})
            
        elseif token.type == "char" and token.value == "}" then
            -- End object
            if #stack > 0 then
                local obj = table.remove(stack)
                if #stack == 0 then
                    -- Root object
                    if obj.tag then
                        table.insert(elements, obj)
                    end
                else
                    -- Nested object, add to parent
                    local parent = stack[#stack]
                    if current_key then
                        parent[current_key] = obj
                        current_key = nil
                    elseif parent.children then
                        table.insert(parent.children, obj)
                    end
                end
            end
            
        elseif token.type == "char" and token.value == "[" then
            -- Start array
            in_array = true
            if #stack > 0 then
                local parent = stack[#stack]
                parent.children = {}
            end
            
        elseif token.type == "char" and token.value == "]" then
            -- End array
            in_array = false
            
        elseif token.type == "string" then
            -- String value (remove quotes)
            local str_value = token.value:sub(2, -2)
            str_value = str_value:gsub('\\"', '"'):gsub('\\\\', '\\'):gsub('\\n', '\n'):gsub('\\r', '\r'):gsub('\\t', '\t')
            
            if current_key then
                -- This is a value for the current key
                if #stack > 0 then
                    local current = stack[#stack]
                    current[current_key] = str_value
                    current_key = nil
                end
            elseif not in_array then
                -- This is a key
                current_key = str_value
            elseif #stack > 0 then
                -- String in array (probably text content)
                local parent = stack[#stack]
                parent.text = str_value
            end
            
        elseif token.type == "number" then
            -- Number value
            local num_value = tonumber(token.value)
            if current_key and #stack > 0 then
                local current = stack[#stack]
                current[current_key] = num_value
                current_key = nil
            end
            
        elseif token.type == "char" and token.value == ":" then
            -- Key-value separator, already handled
        elseif token.type == "char" and token.value == "," then
            -- Next item, reset current_key
            current_key = nil
        end
    end
    
    return elements
end

-- Extract ALL elements from JSON (including nested)
function extract_all_elements(json_str)
    print("[LUA] Extracting elements from JSON...")
    
    -- First, try to find all "tag": "value" patterns (simplest approach)
    local elements = {}
    local element_count = 0
    
    -- Use pattern matching to find tags
    local pos = 1
    while pos <= #json_str do
        -- Look for "tag": pattern
        local tag_start = json_str:find('"tag"%s*:%s*"', pos)
        if not tag_start then break end
        
        -- Find the tag value
        local value_start = tag_start + string.len('"tag"%s*:%s*"')
        local value_end = value_start
        
        while value_end <= #json_str and json_str:sub(value_end, value_end) ~= '"' do
            if json_str:sub(value_end, value_end) == '\\' then
                value_end = value_end + 2  -- Skip escaped quote
            else
                value_end = value_end + 1
            end
        end
        
        if value_end > #json_str then break end
        
        local tag_value = json_str:sub(value_start, value_end - 1)
        
        -- Extract surrounding context for more properties
        local context_start = math.max(1, tag_start - 200)
        local context_end = math.min(#json_str, value_end + 400)
        local context = json_str:sub(context_start, context_end)
        
        -- Extract ID
        local id_value = ""
        local id_pattern = '"id"%s*:%s*"([^"]*)"'
        local id_start = context:find(id_pattern)
        if id_start then
            local id_match = context:match(id_pattern)
            if id_match then
                id_value = id_match
            end
        end
        
        -- Extract type
        local type_value = "block"
        local type_pattern = '"type"%s*:%s*"([^"]*)"'
        local type_start = context:find(type_pattern)
        if type_start then
            local type_match = context:match(type_pattern)
            if type_match then
                type_value = type_match
            end
        end
        
        -- Extract text
        local text_value = ""
        local text_pattern = '"text"%s*:%s*"([^"]*)"'
        local text_start = context:find(text_pattern)
        if text_start then
            local text_match = context:match(text_pattern)
            if text_match then
                text_value = text_match:gsub('\\"', '"'):gsub('\\\\', '\\')
            end
        end
        
        -- Extract parent info (NEW - check if element has parent)
        local parent_info = ""
        local parent_pattern = '"parent"[%s%c]*:[%s%c]*"([^"]*)"'
        local parent_start = context:find(parent_pattern)
        if parent_start then
            local parent_match = context:match(parent_pattern)
            if parent_match then
                parent_info = parent_match
            end
        end
        
        -- Create element record with parent info
        element_count = element_count + 1
        elements[element_count] = {
            tag = tag_value,
            id = id_value,
            type = type_value,
            text = text_value,
            parent = parent_info,
            original_position = tag_start
        }
        
        -- Debug output for first 20 elements
        if element_count <= 20 then
            print(string.format("[LUA] Found: <%s> id='%s' type='%s' parent='%s'", 
                  tag_value, id_value, type_value, parent_info))
            if text_value ~= "" and #text_value > 0 then
                print(string.format("       Text: '%s'", text_value:sub(1, 40) .. (#text_value > 40 and "..." or "")))
            end
        end
        
        pos = value_end + 1
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
    elseif tag == "header" then width = 760; height = 80
    elseif tag == "footer" then width = 760; height = 50
    elseif tag == "nav" then width = 760; height = 60
    elseif tag == "main" then width = 760; height = 200
    elseif tag == "section" then width = 760; height = 150
    elseif tag == "article" then width = 760; height = 180
    
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

-- NEW: Check if element is likely inside a container
function is_inside_container(element, all_elements)
    if element.parent and element.parent ~= "" then
        return true
    end
    
    -- Heuristic: if previous element is a container tag
    for _, elem in ipairs(all_elements) do
        if elem.original_position and element.original_position and
           elem.original_position < element.original_position and
           (elem.tag == "div" or elem.tag == "section" or elem.tag == "article" or 
            elem.tag == "header" or elem.tag == "footer" or elem.tag == "main") then
            return true
        end
    end
    
    return false
end

-- NEW: Check if element is a container itself
function is_container_element(element)
    return element.tag == "table" or element.tag == "form" or 
           element.tag == "ul" or element.tag == "ol" or 
           element.tag == "menu" or element.tag == "div" or
           element.tag == "section" or element.tag == "article" or
           element.tag == "header" or element.tag == "footer" or
           element.tag == "main" or element.tag == "nav"
end

-- ========== FIXED MAIN FUNCTION ==========
function M.calculate_positions(json_str)
    print("[LUA] Starting FIXED hierarchical position calculation...")
    print("[LUA] JSON length: " .. #json_str .. " bytes")
    
    -- Extract all elements
    local all_elements = extract_all_elements(json_str)
    
    if #all_elements == 0 then
        print("[LUA] ERROR: No elements found!")
        return [[{
    "success": false,
    "message": "No elements found in JSON",
    "element_count": 0,
    "elements": []
}]]
    end
    
    -- Group elements by likely hierarchy
    local positioned_elements = {}
    local current_y = 20
    local margin = M.config.margin_between_elements
    
    -- Track current container depth
    local current_indent = M.config.regular_indent
    local in_container = false
    local container_start_y = 0
    local container_height = 0
    
    print("[LUA] Calculating positions with hierarchy awareness...")
    
    for i, element in ipairs(all_elements) do
        local width, height = calculate_dimensions(element)
        
        -- Check if this element starts a new container
        if is_container_element(element) then
            if in_container then
                -- Close previous container
                current_y = container_start_y + container_height + margin
            end
            
            in_container = true
            container_start_y = current_y
            container_height = height
            current_indent = M.config.container_indent
            
            -- Special handling for body (always at 0,0)
            if element.tag == "body" then
                positioned_elements[i] = {
                    tag = element.tag,
                    id = element.id,
                    type = element.type,
                    x = 0,
                    y = 0,
                    calculated_width = width,
                    calculated_height = height,
                    is_container = true
                }
                print(string.format("[LUA] Body at (0, 0) %dx%d", width, height))
            else
                positioned_elements[i] = {
                    tag = element.tag,
                    id = element.id,
                    type = element.type,
                    x = current_indent,
                    y = current_y,
                    calculated_width = width,
                    calculated_height = height,
                    is_container = true
                }
                
                current_y = current_y + height + margin
            end
            
        -- Elements likely inside containers get more indent
        elseif is_inside_container(element, all_elements) then
            positioned_elements[i] = {
                tag = element.tag,
                id = element.id,
                type = element.type,
                x = current_indent + 20, -- Extra indent for nested
                y = current_y,
                calculated_width = width,
                calculated_height = height,
                is_nested = true
            }
            
            current_y = current_y + height + margin
            
        -- Regular elements
        else
            positioned_elements[i] = {
                tag = element.tag,
                id = element.id,
                type = element.type,
                x = current_indent,
                y = current_y,
                calculated_width = width,
                calculated_height = height
            }
            
            current_y = current_y + height + margin
        end
        
        -- Debug output for first 25 elements
        if i <= 25 then
            local indent_str = positioned_elements[i].is_nested and " (nested)" or 
                              positioned_elements[i].is_container and " (container)" or ""
            print(string.format("[LUA] %3d. %-10s%s x=%-4d y=%-4d %dx%d%s",
                  i, "<" .. element.tag .. ">", 
                  element.id ~= "" and " #" .. element.id or "",
                  positioned_elements[i].x, positioned_elements[i].y,
                  width, height, indent_str))
        end
    end
    
    print(string.format("[LUA] Final Y position: %d pixels", current_y))
    print("[LUA] Total elements positioned: " .. #positioned_elements)
    
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
            "width": %d,
            "height": %d
        }]], 
        elem.tag, elem.id or "", elem.type or "block", 
        elem.x, elem.y, elem.calculated_width, elem.calculated_height))
    end
    
    local result = string.format([[
{
    "success": true,
    "message": "Positions calculated with hierarchy",
    "element_count": %d,
    "elements": [
        %s
    ]
}
]], #positioned_elements, table.concat(elements_json, ",\n"))
    
    return result
end

-- Export module
return M
