// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "inspector.hpp"

#include <ostream>

namespace tfh::inspect {

// Machine-readable report (JSON).
void writeJsonReport(const Result& r, const std::string& inputName, std::ostream& os);
// Short human-readable summary for the console.
void writeSummary(const Result& r, std::ostream& os);

} // namespace tfh::inspect
