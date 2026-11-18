/**
 * @file
 * @brief Implementation for uanlz_log2json/src/output/output_base.
 */
#include "output_base.h"

using namespace UANLZ::LOG2JSON::Output;

OutputBase::OutputBase() {}

OutputBase::~OutputBase() {}

std::string OutputBase::getDescription() const
{
    return description;
}
