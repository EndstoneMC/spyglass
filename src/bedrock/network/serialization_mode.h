#pragma once

namespace cereal {
}

enum class SerializationMode : int {
    ManualOnly = 0,
    SideBySide_LogOnMismatch = 1,
    SideBySide_AssertOnMismatch = 2,
    SemanticSideBySide_LogOnMismatch = 3,
    SemanticSideBySide_AssertOnMismatch = 4,
    CerealOnly = 5,
};
