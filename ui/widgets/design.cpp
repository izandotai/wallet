#include "ui/widgets/design.hpp"

namespace izan::ui {

namespace {

    DesignLanguage g_design = design_cupertino();

}

const DesignLanguage& design()
{
    return g_design;
}

void set_design(const DesignLanguage& language)
{
    g_design = language;
}

DesignLanguage design_cupertino()
{
    // The defaults in the struct ARE Cupertino — the language the
    // wallet ships with.
    return DesignLanguage {};
}

}
