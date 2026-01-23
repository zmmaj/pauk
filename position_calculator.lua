-- position_calculator.lua
local M = {}

-- Simple recursive function to process nodes
local function process_node(node, results, x, y, width, is_inline_context)
    if not node or type(node) ~= "table" then
        return y
    end
    
    local node_type = node.type or ""
    local tag = node.tag or ""
    local text = node.text or ""
    local children = node.children or {}
    
    -- Clean up text
    if text then
        text = text:gsub("%s+", " "):gsub("^%s+", ""):gsub("%s+$", "")
    end
    
    -- Handle text nodes
    if node_type == "text" or tag == "" then
        if text and text ~= "" then
            -- Simple text output
            table.insert(results, {
                x = x,
                y = y,
                text = text,
                type = "text"
            })
            return y + 1
        end
        return y
    end
    
    -- Handle elements
    local is_block = false
    
    -- Simple block element detection
    if tag == "div" or tag == "p" or tag == "h1" or tag == "h2" or tag == "h3" or
       tag == "ul" or tag == "ol" or tag == "li" or tag == "table" or
       tag == "tr" or tag == "td" or tag == "form" or tag == "header" or
       tag == "footer" or tag == "article" or tag == "section" then
        is_block = true
    end
    
    if is_block then
        -- Block element - start on new line
        table.insert(results, {
            x = x,
            y = y,
            text = "[" .. string.upper(tag) .. "]",
            type = "element_start",
            tag = tag
        })
        
        local child_y = y + 1
        
        -- Process children with indentation
        for i, child in ipairs(children) do
            child_y = process_node(child, results, x + 2, child_y, width - 2, false)
        end
        
        -- Add closing tag
        table.insert(results, {
            x = x,
            y = child_y,
            text = "[/" .. string.upper(tag) .. "]",
            type = "element_end",
            tag = tag
        })
        
        return child_y + 1
        
    else
        -- Inline element - continue on same line
        if not is_inline_context then
            -- Start inline context
            table.insert(results, {
                x = x,
                y = y,
                text = "<" .. tag .. ">",
                type = "inline_start",
                tag = tag
            })
            
            local inline_y = y
            
            -- Process inline children
            for i, child in ipairs(children) do
                inline_y = process_node(child, results, x + #tag + 2, inline_y, width, true)
            end
            
            -- Close inline element
            table.insert(results, {
                x = x + #tag + 2,
                y = inline_y,
                text = "</" .. tag .. ">",
                type = "inline_end",
                tag = tag
            })
            
            return inline_y + 1
        else
            -- Already in inline context, just add text
            if text and text ~= "" then
                table.insert(results, {
                    x = x,
                    y = y,
                    text = text,
                    type = "inline_text",
                    tag = tag
                })
                return y + 1
            end
            
            -- Process children
            for i, child in ipairs(children) do
                y = process_node(child, results, x, y, width, true)
            end
            
            return y
        end
    end
end

function M.calculate(nodes)
    if not nodes or type(nodes) ~= "table" or #nodes == 0 then
        return {
            success = false,
            message = "No nodes to process",
            elements = {}
        }
    end
    
    local results = {}
    local y = 0
    local width = 80
    
    -- Process all top-level nodes
    for i, node in ipairs(nodes) do
        y = process_node(node, results, 0, y, width, false)
    end
    
    return {
        success = true,
        elements = results,
        element_count = #results,
        message = string.format("Processed %d elements", #results)
    }
end

-- Simple alias
function M.calculate_positions(nodes)
    local result = M.calculate(nodes)
    if result.success then
        return result.elements
    else
        return {}
    end
end

return M
