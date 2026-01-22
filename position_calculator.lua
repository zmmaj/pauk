-- position_calculator.lua - FIXED to read correct file
print("=== LUA POSITION CALCULATOR ===")

-- Configuration
local config = {
    viewport_width = 800,
    viewport_height = 600,
    default_font_size = 16,
    line_height = 1.2,
    container_padding = 15,
    element_margin = 10
}

-- Main calculation function
function calculate_positions(json_str, cfg)
    print("[LUA] calculate_positions FUNCTION CALLED!")
    
    -- DEBUG: Save what C sent us
    local debug_file = io.open("lua_received_from_c.json", "w")
    if debug_file then
        debug_file:write(json_str:sub(1, 500))  -- First 500 chars
        debug_file:close()
        print("[LUA] Saved first 500 chars to lua_received_from_c.json")
    end
    
    print("[LUA] JSON length from C: " .. #json_str)
    
    -- Also try reading from file directly
    print("[LUA] Also trying to read text.html.txt directly...")
    local file = io.open("text.html.txt", "r")
    if file then
        local file_content = file:read("*a")
        file:close()
        print("[LUA] text.html.txt length: " .. #file_content)
        
        -- Use file content if C sent empty/incorrect JSON
        if #json_str < 100 and #file_content > 100 then
            print("[LUA] Using file content instead of C input")
            json_str = file_content
        end
    else
        print("[LUA] Could not open text.html.txt")
    end
    
    -- Update config if provided
    if cfg then
        for k, v in pairs(cfg) do
            config[k] = v
        end
    end
    
    -- ========== BETTER PARSER ==========
    print("[LUA] Parsing JSON...")
    
    local elements = {}
    local found_count = 0
    
    -- Look for ALL "element_id" patterns
    for element_id, parent_id, tag in json_str:gmatch('"element_id":(%d+).-"parent_id":(%d+).-"tag":"([^"]+)"') do
        local id_num = tonumber(element_id)
        local parent_num = tonumber(parent_id)
        
        if id_num and parent_num and tag then
            found_count = found_count + 1
            
            elements[#elements + 1] = {
                element_id = id_num,
                parent_id = parent_num,
                tag = tag,
                x = 0,
                y = 0,
                width = 100,
                height = 40
            }
            
            if found_count <= 5 then
                print(string.format("  [%d] <%s> id=%d parent=%d", 
                      found_count, tag, id_num, parent_num))
            end
        end
    end
    
    -- Alternative search if above fails
    if found_count == 0 then
        print("[LUA] Pattern 1 failed, trying alternative...")
        
        -- Look for just tags
        for tag in json_str:gmatch('"tag":"([^"]+)"') do
            if tag ~= "type" and tag ~= "id" and #tag > 1 then
                found_count = found_count + 1
                
                elements[#elements + 1] = {
                    element_id = found_count,
                    parent_id = 0,
                    tag = tag,
                    x = 0,
                    y = 0,
                    width = 100,
                    height = 40
                }
                
                if found_count <= 5 then
                    print("  Found tag: " .. tag)
                end
                
                if found_count > 50 then break end  -- Limit
            end
        end
    end
    
    print("[LUA] Total elements found: " .. found_count)
    
    -- ========== SIMPLE POSITION CALCULATION ==========
    if found_count == 0 then
        print("[LUA] ERROR: No elements found!")
        print("[LUA] JSON sample (first 200 chars):")
        print("  " .. json_str:sub(1, 200))
        
        -- Return error
        return {
            success = false,
            message = "No elements found in JSON",
            element_count = 0,
            elements = {}
        }
    end
    
    -- Simple layout
    for i, elem in ipairs(elements) do
        elem.x = config.container_padding + ((elem.parent_id or 0) * 20)
        elem.y = config.container_padding + (i * 60)
        
        -- Adjust size
        if elem.tag == "body" then
            elem.width = config.viewport_width
            elem.height = config.viewport_height
        elseif elem.tag == "div" then
            elem.width = 700
            elem.height = 100
        elseif elem.tag == "h1" then
            elem.width = 700
            elem.height = 60
        elseif elem.tag == "h2" then
            elem.width = 680
            elem.height = 48
        end
    end
    
    -- Prepare return value
    local return_elements = {}
    for _, elem in ipairs(elements) do
        table.insert(return_elements, {
            tag = elem.tag,
            element_id = elem.element_id,
            parent_id = elem.parent_id,
            x = elem.x,
            y = elem.y,
            calculated_width = elem.width,
            calculated_height = elem.height
        })
    end
    
    print("[LUA] Calculated positions for " .. #return_elements .. " elements")
    
    return {
        success = true,
        message = "Positioned " .. #return_elements .. " elements",
        element_count = #return_elements,
        elements = return_elements
    }
end

print("=== LUA SCRIPT LOADED ===")

-- Test mode
if arg and arg[0]:match("position_calculator.lua") then
    print("\n[TEST] Running directly...")
    
    -- Test with actual file
    local file = io.open("text.html.txt", "r")
    if file then
        local content = file:read("*a")
        file:close()
        
        local result = calculate_positions(content, config)
        print("Test: " .. result.message)
    else
        print("ERROR: text.html.txt not found!")
    end
end-- position_calculator.lua - FIXED to read correct file
print("=== LUA POSITION CALCULATOR ===")

-- Configuration
local config = {
    viewport_width = 800,
    viewport_height = 600,
    default_font_size = 16,
    line_height = 1.2,
    container_padding = 15,
    element_margin = 10
}

-- Main calculation function
function calculate_positions(json_str, cfg)
    print("[LUA] calculate_positions FUNCTION CALLED!")
    
    -- DEBUG: Save what C sent us
    local debug_file = io.open("lua_received_from_c.json", "w")
    if debug_file then
        debug_file:write(json_str:sub(1, 500))  -- First 500 chars
        debug_file:close()
        print("[LUA] Saved first 500 chars to lua_received_from_c.json")
    end
    
    print("[LUA] JSON length from C: " .. #json_str)
    
    -- Also try reading from file directly
    print("[LUA] Also trying to read text.html.txt directly...")
    local file = io.open("text.html.txt", "r")
    if file then
        local file_content = file:read("*a")
        file:close()
        print("[LUA] text.html.txt length: " .. #file_content)
        
        -- Use file content if C sent empty/incorrect JSON
        if #json_str < 100 and #file_content > 100 then
            print("[LUA] Using file content instead of C input")
            json_str = file_content
        end
    else
        print("[LUA] Could not open text.html.txt")
    end
    
    -- Update config if provided
    if cfg then
        for k, v in pairs(cfg) do
            config[k] = v
        end
    end
    
    -- ========== BETTER PARSER ==========
    print("[LUA] Parsing JSON...")
    
    local elements = {}
    local found_count = 0
    
    -- Look for ALL "element_id" patterns
    for element_id, parent_id, tag in json_str:gmatch('"element_id":(%d+).-"parent_id":(%d+).-"tag":"([^"]+)"') do
        local id_num = tonumber(element_id)
        local parent_num = tonumber(parent_id)
        
        if id_num and parent_num and tag then
            found_count = found_count + 1
            
            elements[#elements + 1] = {
                element_id = id_num,
                parent_id = parent_num,
                tag = tag,
                x = 0,
                y = 0,
                width = 100,
                height = 40
            }
            
            if found_count <= 5 then
                print(string.format("  [%d] <%s> id=%d parent=%d", 
                      found_count, tag, id_num, parent_num))
            end
        end
    end
    
    -- Alternative search if above fails
    if found_count == 0 then
        print("[LUA] Pattern 1 failed, trying alternative...")
        
        -- Look for just tags
        for tag in json_str:gmatch('"tag":"([^"]+)"') do
            if tag ~= "type" and tag ~= "id" and #tag > 1 then
                found_count = found_count + 1
                
                elements[#elements + 1] = {
                    element_id = found_count,
                    parent_id = 0,
                    tag = tag,
                    x = 0,
                    y = 0,
                    width = 100,
                    height = 40
                }
                
                if found_count <= 5 then
                    print("  Found tag: " .. tag)
                end
                
                if found_count > 50 then break end  -- Limit
            end
        end
    end
    
    print("[LUA] Total elements found: " .. found_count)
    
    -- ========== SIMPLE POSITION CALCULATION ==========
    if found_count == 0 then
        print("[LUA] ERROR: No elements found!")
        print("[LUA] JSON sample (first 200 chars):")
        print("  " .. json_str:sub(1, 200))
        
        -- Return error
        return {
            success = false,
            message = "No elements found in JSON",
            element_count = 0,
            elements = {}
        }
    end
    
    -- Simple layout
    for i, elem in ipairs(elements) do
        elem.x = config.container_padding + ((elem.parent_id or 0) * 20)
        elem.y = config.container_padding + (i * 60)
        
        -- Adjust size
        if elem.tag == "body" then
            elem.width = config.viewport_width
            elem.height = config.viewport_height
        elseif elem.tag == "div" then
            elem.width = 700
            elem.height = 100
        elseif elem.tag == "h1" then
            elem.width = 700
            elem.height = 60
        elseif elem.tag == "h2" then
            elem.width = 680
            elem.height = 48
        end
    end
    
    -- Prepare return value
    local return_elements = {}
    for _, elem in ipairs(elements) do
        table.insert(return_elements, {
            tag = elem.tag,
            element_id = elem.element_id,
            parent_id = elem.parent_id,
            x = elem.x,
            y = elem.y,
            calculated_width = elem.width,
            calculated_height = elem.height
        })
    end
    
    print("[LUA] Calculated positions for " .. #return_elements .. " elements")
    
    return {
        success = true,
        message = "Positioned " .. #return_elements .. " elements",
        element_count = #return_elements,
        elements = return_elements
    }
end

print("=== LUA SCRIPT LOADED ===")

-- Test mode
if arg and arg[0]:match("position_calculator.lua") then
    print("\n[TEST] Running directly...")
    
    -- Test with actual file
    local file = io.open("text.html.txt", "r")
    if file then
        local content = file:read("*a")
        file:close()
        
        local result = calculate_positions(content, config)
        print("Test: " .. result.message)
    else
        print("ERROR: text.html.txt not found!")
    end
end
