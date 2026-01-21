-- position_calculator_final.lua - For COMPLETE Helen OS output
print("=== LUA POSITION CALCULATOR (FOR COMPLETE OUTPUT) ===")

local M = {}

M.config = {
    viewport_width = 800,
    viewport_height = 600,
    default_font_size = 16,
    margin_between_elements = 15
}

-- Extract element with ALL properties
function extract_element_complete(obj_str)
    local element = {}
    
    -- Basic properties
    element.tag = obj_str:match('"tag"%s*:%s*"([^"]+)"') or ""
    element.id = obj_str:match('"id"%s*:%s*"([^"]*)"') or ""
    element.type = obj_str:match('"type"%s*:%s*"([^"]*)"') or "block"
    element.text = obj_str:match('"text"%s*:%s*"([^"]*)"') or ""
    
    -- File references (CRITICAL - from your new output)
    element.form_file = obj_str:match('"form_file"%s*:%s*"([^"]*)"') or ""
    element.list_file = obj_str:match('"list_file"%s*:%s*"([^"]*)"') or ""
    element.table_file = obj_str:match('"table_file"%s*:%s*"([^"]*)"') or ""
    
    -- Existing layout from C engine
    element.x = tonumber(obj_str:match('"x"%s*:%s*([%d%.]+)')) or 0
    element.y = tonumber(obj_str:match('"y"%s*:%s*([%d%.]+)')) or 0
    element.layout_width = tonumber(obj_str:match('"layout_width"%s*:%s*([%d%.]+)')) or 0
    element.layout_height = tonumber(obj_str:match('"layout_height"%s*:%s*([%d%.]+)')) or 0
    
    -- Boolean flags
    element.is_form = obj_str:find('"is_form"%s*:%s*true') ~= nil
    element.is_list = obj_str:find('"is_list"%s*:%s*true') ~= nil
    element.is_table = obj_str:find('"is_table"%s*:%s*true') ~= nil
    element.is_heading = obj_str:find('"is_heading"%s*:%s*true') ~= nil
    
    return element
end

-- Calculate final dimensions
function calculate_final_dimensions(element)
    local width, height
    
    -- Priority 1: Use existing layout if available
    if element.layout_width > 0 and element.layout_height > 0 then
        width = element.layout_width
        height = element.layout_height
        return width, height, "layout"
    end
    
    -- Priority 2: Special elements with file references
    if element.form_file ~= "" then
        width = 400  -- From your form file
        height = 130 -- From your form file
        return width, height, "form_file"
    elseif element.list_file ~= "" then
        width = 400  -- From your list file
        height = 95  -- From your list file
        return width, height, "list_file"
    elseif element.table_file ~= "" then
        width = 280  -- From your table file
        height = 160 -- From your table file
        return width, height, "table_file"
    end
    
    -- Priority 3: Calculate based on element type and text
    if element.tag == "body" then
        width = M.config.viewport_width
        height = M.config.viewport_height
    elseif element.is_heading then
        if element.tag == "h1" then width = 760; height = 60
        elseif element.tag == "h2" then width = 760; height = 48
        elseif element.tag == "h3" then width = 760; height = 36
        else width = 760; height = 30 end
    elseif element.tag == "p" then
        width = 760
        if #element.text > 0 then
            height = 20 + (math.ceil(#element.text / 80) * 20)
        else
            height = 40
        end
    elseif element.tag == "img" then width = 200; height = 150
    elseif element.tag == "div" then width = 760; height = 100
    else
        width = 200; height = 40  -- Default
    end
    
    return width, height, "calculated"
end

-- Main function
function M.calculate_positions(json_str)
    print("[LUA] Processing COMPLETE Helen OS output...")
    
    local all_elements = {}
    local pos = 1
    local element_count = 0
    
    -- Extract all elements
    while pos <= #json_str do
        local obj_start = json_str:find('{', pos)
        if not obj_start then break end
        
        local depth = 1
        local obj_end = obj_start + 1
        
        while obj_end <= #json_str and depth > 0 do
            local char = json_str:sub(obj_end, obj_end)
            if char == '{' then depth = depth + 1
            elseif char == '}' then depth = depth - 1 end
            obj_end = obj_end + 1
        end
        
        if depth == 0 then
            local obj_str = json_str:sub(obj_start, obj_end - 1)
            local element = extract_element_complete(obj_str)
            
            if element.tag ~= "" then
                element_count = element_count + 1
                all_elements[element_count] = element
                
                -- Debug first 10 elements
                if element_count <= 10 then
                    local has_file = element.form_file ~= "" or 
                                    element.list_file ~= "" or 
                                    element.table_file ~= ""
                    local file_info = has_file and " [HAS FILE]" or ""
                    print(string.format("[LUA] %d. <%s>%s at (%.1f, %.1f)%s",
                          element_count, element.tag, element.id ~= "" and " #"..element.id or "",
                          element.x, element.y, file_info))
                end
            end
            
            pos = obj_end
        else
            pos = obj_start + 1
        end
    end
    
    print("[LUA] Found " .. element_count .. " total elements")
    
    -- Calculate final positions
    local elements_json = {}
    for i, elem in ipairs(all_elements) do
        local width, height, source = calculate_final_dimensions(elem)
        
        -- Enhance position: add margin between elements
        local enhanced_y = elem.y
        if i > 1 and elem.tag ~= "body" then
            enhanced_y = enhanced_y + ((i-1) * M.config.margin_between_elements)
        end
        
        table.insert(elements_json, string.format([[
        {
            "tag": "%s",
            "id": "%s",
            "type": "%s",
            "x": %.1f,
            "y": %.1f,
            "calculated_width": %.1f,
            "calculated_height": %.1f,
            "dimension_source": "%s"
        }]], 
        elem.tag, elem.id, elem.type, 
        elem.x, enhanced_y, width, height, source))
    end
    
    local result = string.format([[
{
    "success": true,
    "message": "Enhanced %d elements with file references",
    "document_width": %d,
    "document_height": %d,
    "element_count": %d,
    "elements": [%s]
}]],
    #all_elements,
    M.config.viewport_width,
    M.config.viewport_height,
    #all_elements,
    table.concat(elements_json, ",\n"))
    
    return result
end

print("=== LUA POSITION CALCULATOR READY ===")
return M