namespace HiveWE.Mcp;

/// <summary>Sent to the client as the MCP server instructions: the conventions every tool shares.</summary>
public static class Instructions
{
    public const string Text = """
        These tools drive the HiveWE Warcraft III world editor while it runs. Every change shows up in the editor
        immediately. Nothing is written to disk until hivewe_map save (or test/export).

        Conventions:
        - Positions are world coordinates in game units, the same as JASS/Lua (GetUnitX). A map of W x H tiles spans
          offset_x .. offset_x + W*128 horizontally (see hivewe_map info). 1 tile = 128 units.
        - Brush sizes and terrain areas are in tiles. Facing and angles are in degrees.
        - Placed doodads, units, items and regions are identified by "id" (their creation number). "type" is the
          four letter object id, e.g. hfoo (Footman), LTlt (Lordaeron tree), ratc (Claws of Attack).
        - Find object type ids with hivewe_objects action=list (kind unit/item/doodad/destructible/ability/upgrade/buff,
          search by name). Custom objects are made with hivewe_objects action=create from a standard base.
        - Map edits (terrain, pathing, doodads, units, items, regions) are undoable with hivewe_undo. hivewe_batch runs
          many calls at once and undoes as one step.
        - Look at the result with hivewe_screenshot after visual changes.
        - Scripts are JASS or Lua (hivewe_map_info script_language). Custom code goes in the map header
          (hivewe_script action=set_header) or in script triggers (hivewe_triggers). A trigger named "My Trigger" must
          define function InitTrig_My_Trigger. hivewe_script action=build compiles and reports errors.
        - GUI triggers can't be edited, only replaced by script triggers.
        """;
}
