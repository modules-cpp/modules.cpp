// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string_view>

export module mm.shell:status;

export namespace mm::shell {

enum class Status {
    Ok,
    BadArgument,
    Duplicate,
    NotFound,
    Unavailable,
    Unsupported,
    Overflow,
    WriteError,
    ReadError,
};

enum class CommandStatus : int {
    Ok = 0,
    Failure = 1,
    Usage = 2,
    Unavailable = 125,
    CannotExecute = 126,
    NotFound = 127,
};

enum class Flow {
    Normal,
    Exit,
    Break,
    Continue,
    Return,
    Replace,
    Yield,
};

enum class CommandClass {
    SpecialBuiltin,
    Builtin,
    Regular,
    Custom,
};

enum class StorageClass {
    SourceBytes,
    ExpansionScratch,
    Tokens,
    WordFragments,
    SyntaxNodes,
    SyntaxLinks,
    ParserContext,
    EvaluatorFrames,
    Variables,
    VariableText,
    PositionalParameters,
    PositionalParameterText,
    ExpandedFields,
    ExpansionPieces,
    ExpandedFieldText,
    Functions,
    FunctionArena,
    Scripts,
    ScriptArena,
    CustomCommands,
    CaptureBytes,
    StagedOutput,
    ConsolePendingOutput,
    TransactionScratch,
    InteractiveInputLine,
};

struct OverflowInfo {
    StorageClass storage_class = StorageClass::SourceBytes;
    std::size_t required = 0;
};

struct CommandResult {
    Flow flow = Flow::Normal;
    int status = 0;
    Status error = Status::Ok;
    OverflowInfo overflow{};
};

struct InstallResult {
    Status status = Status::Ok;
    OverflowInfo overflow{};

    [[nodiscard]] constexpr bool ok() const {
        return status == Status::Ok;
    }

    [[nodiscard]] constexpr bool operator==(Status s) const {
        return status == s;
    }
};

[[nodiscard]] constexpr std::string_view describe(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::BadArgument: return "bad argument";
        case Status::Duplicate: return "duplicate entry";
        case Status::NotFound: return "not found";
        case Status::Unavailable: return "service or capability unavailable";
        case Status::Unsupported: return "unsupported operation";
        case Status::Overflow: return "storage overflow";
        case Status::WriteError: return "i/o write error";
        case Status::ReadError: return "i/o read error";
    }
    return "unknown status";
}

[[nodiscard]] constexpr std::string_view describe(StorageClass storage_class) {
    switch (storage_class) {
        case StorageClass::SourceBytes: return "source bytes";
        case StorageClass::ExpansionScratch: return "expansion scratch";
        case StorageClass::Tokens: return "tokens";
        case StorageClass::WordFragments: return "word fragments";
        case StorageClass::SyntaxNodes: return "syntax nodes";
        case StorageClass::SyntaxLinks: return "syntax links";
        case StorageClass::ParserContext: return "parser context";
        case StorageClass::EvaluatorFrames: return "evaluator frames";
        case StorageClass::Variables: return "variables";
        case StorageClass::VariableText: return "variable text";
        case StorageClass::PositionalParameters: return "positional parameters";
        case StorageClass::PositionalParameterText: return "positional text";
        case StorageClass::ExpandedFields: return "expanded fields";
        case StorageClass::ExpansionPieces: return "expansion pieces";
        case StorageClass::ExpandedFieldText: return "expanded text";
        case StorageClass::Functions: return "functions";
        case StorageClass::FunctionArena: return "function arena";
        case StorageClass::Scripts: return "scripts";
        case StorageClass::ScriptArena: return "script arena";
        case StorageClass::CustomCommands: return "custom commands";
        case StorageClass::CaptureBytes: return "capture bytes";
        case StorageClass::StagedOutput: return "staged output";
        case StorageClass::ConsolePendingOutput: return "console output";
        case StorageClass::TransactionScratch: return "transaction scratch";
        case StorageClass::InteractiveInputLine: return "interactive line";
    }
    return "unknown storage";
}

}  // namespace mm::shell
