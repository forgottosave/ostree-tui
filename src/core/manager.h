/*_____________________________________________________________
 | Manager Render
 |   Right portion of main window, includes branch filter &
 |   detailed commit info of the selected commit. In future
 |   different modes should be supported (like a rebase mode
 |   exchangeable with the commit info)
 |___________________________________________________________*/

#include <string>
#include <unordered_map>
#include <vector>

#include "ftxui/component/component.hpp"  // for Component

#include "commit.h"
#include "../util/cpplibostree.h"

/// Right postion of main window
class Manager {
public:
    ftxui::Component branch_boxes = ftxui::Container::Vertical({});

public:
    Manager(cpplibostree::OSTreeRepo repo, std::unordered_map<std::string, bool> *visible_branches);
    
    ftxui::Element branchBoxRender();
    ftxui::Element render(Commit display_commit);
};
