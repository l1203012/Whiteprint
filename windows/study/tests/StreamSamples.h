#pragma once
// Hand-written in Claude Code's `--output-format stream-json --verbose` shape (shared by the study tests).
#include <QString>
#include <QStringList>

namespace wp::testing::StreamSamples {

inline const QString initLine = QStringLiteral(R"JSON({"type":"system","subtype":"init","cwd":"C:\\Users\\x\\AppData\\Local\\Temp\\Whiteprint-1","session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11","tools":["mcp__whiteprint__list_imports","mcp__whiteprint__read_chunk","mcp__whiteprint__save_points","mcp__whiteprint__get_points","mcp__whiteprint__build_study_plan","ReadMcpResourceTool"],"mcp_servers":[{"name":"whiteprint","status":"connected"}],"model":"claude-sonnet-4-5","permissionMode":"dontAsk","apiKeySource":"none"})JSON");

inline QStringList run()
{
    return {
        initLine,
        R"JSON({"type":"assistant","message":{"id":"msg_01","type":"message","role":"assistant","model":"claude-sonnet-4-5","content":[{"type":"text","text":"I'll start by listing your imports."},{"type":"tool_use","id":"toolu_01","name":"mcp__whiteprint__list_imports","input":{}}],"stop_reason":"tool_use","usage":{"input_tokens":1200,"output_tokens":40}},"parent_tool_use_id":null,"session_id":"5f1c6c1e"})JSON",
        R"JSON({"type":"user","message":{"role":"user","content":[{"tool_use_id":"toolu_01","type":"tool_result","content":[{"type":"text","text":"i1 Lecture3.pptx 24 12 0/12"}]}]},"parent_tool_use_id":null,"session_id":"5f1c6c1e"})JSON",
        R"JSON({"type":"assistant","message":{"id":"msg_02","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_02","name":"mcp__whiteprint__read_chunk","input":{"import":"i1","n":3}}]},"parent_tool_use_id":null,"session_id":"5f1c6c1e"})JSON",
        R"JSON({"type":"user","message":{"role":"user","content":[{"tool_use_id":"toolu_02","type":"tool_result","content":[{"type":"text","text":"Lecture3.pptx · chunk 3/12 · slide 5–slide 7\n\n[slide 5]\nTCP"}]}]},"session_id":"5f1c6c1e"})JSON",
        R"JSON({"type":"assistant","message":{"id":"msg_03","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_03","name":"mcp__whiteprint__save_points","input":{"import":"i1","n":3,"points":[{"text":"TCP is connection-oriented","importance":"must","ref":"slide 5"}]}}]},"session_id":"5f1c6c1e"})JSON",
        R"JSON({"type":"user","message":{"role":"user","content":[{"tool_use_id":"toolu_03","type":"tool_result","content":[{"type":"text","text":"ok"}]}]},"session_id":"5f1c6c1e"})JSON",
        R"JSON({"type":"assistant","message":{"id":"msg_04","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_04","name":"ReadMcpResourceTool","input":{"server":"whiteprint","uri":"whiteprint://dsl"}}]},"session_id":"5f1c6c1e"})JSON",
        R"JSON({"type":"assistant","message":{"id":"msg_05","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_05","name":"mcp__whiteprint__get_points","input":{}}]},"session_id":"5f1c6c1e"})JSON",
        R"JSON({"type":"assistant","message":{"id":"msg_06","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_06","name":"mcp__whiteprint__build_study_plan","input":{"imports":["i1"],"plan":{"title":"Networks"}}}]},"session_id":"5f1c6c1e"})JSON",
        R"JSON({"type":"assistant","message":{"id":"msg_07","type":"message","role":"assistant","content":[{"type":"text","text":"Study plan ready: 4 modules, 3 tasks."}]},"session_id":"5f1c6c1e"})JSON",
        R"JSON({"type":"result","subtype":"success","is_error":false,"duration_ms":81234,"duration_api_ms":79012,"num_turns":9,"result":"Study plan ready: 4 modules, 3 tasks.","session_id":"5f1c6c1e","total_cost_usd":0.21,"usage":{"input_tokens":30211,"output_tokens":2210},"permission_denials":[]})JSON",
    };
}

inline const QString notLoggedIn = QStringLiteral(R"JSON({"type":"result","subtype":"success","is_error":true,"duration_ms":312,"num_turns":1,"result":"Invalid API key · Please run /login","session_id":"0b6a","total_cost_usd":0})JSON");
inline const QString maxTurns = QStringLiteral(R"JSON({"type":"result","subtype":"error_max_turns","is_error":true,"duration_ms":1000,"num_turns":50,"session_id":"0b6a"})JSON");
inline const QString helperFailed = QStringLiteral(R"JSON({"type":"system","subtype":"init","session_id":"0b6a","tools":[],"mcp_servers":[{"name":"whiteprint","status":"failed"}]})JSON");

} // namespace wp::testing::StreamSamples
