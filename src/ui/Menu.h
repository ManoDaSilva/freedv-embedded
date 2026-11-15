#pragma once

#include <functional>
#include <string>
#include <vector>

#include "display/Framebuffer.h"
#include "ui/MenuInput.h"

namespace fdve {

// One entry of a menu. A tree of these describes the whole menu; the Menu
// class below walks it. Nothing here knows about the settings themselves.
struct MenuItem
{
    enum class Kind { Submenu, Choice, Number, Text, Action, Info };

    Kind kind = Kind::Action;
    std::string label;

    // Submenu
    std::vector<MenuItem> children;
    // Called just before the operator enters this submenu. It may replace
    // "children", for menus whose rows depend on the moment (network interfaces).
    std::function<void(MenuItem&)> onEnter;
    std::function<void()> onLeave; // called when the operator leaves this submenu
    bool startOnBack = false;      // open with the back entry highlighted (confirmation menus)

    // Choice, Text and Info: the value as text. Info rows only show it; it
    // is read again at every redraw, so it can change while on screen.
    std::function<std::string()> get;
    std::function<void(const std::string&)> set;
    std::vector<std::string> choices;
    std::vector<std::string> choiceLabels; // what to show for each choice; empty = the choice itself
    int maxLength = 8;                     // Text

    // Number
    std::function<int()> getInt;
    std::function<void(int)> setInt;
    int min = 0;
    int max = 100;
    int step = 1;
    std::string unit;
    bool live = false; // apply while turning (volume), not only on confirm

    // Action
    std::function<void()> action;
};

// Menu navigation and drawing for a rotary encoder with a push button:
//   turn         move the selection, or change the value being edited
//   press        enter, edit, confirm
//   long press   back, or cancel the edit
// Every menu also starts with a "back" entry, for knobs where a long press is awkward.
//
// The drawing follows the same rule as the status screen: a header in the top
// 16 rows, row 16 empty, the rest below, so it suits two-colour displays.
class Menu
{
public:
    explicit Menu(MenuItem root);
    void setRoot(MenuItem root); // replaces the tree and closes the menu

    bool active() const { return active_; }
    void open();
    void close();

    bool handle(const MenuEvents& events); // true when the screen needs redrawing
    void render(Framebuffer& fb) const;

    // Label of the highlighted entry ("" for the back entry or when closed).
    std::string selectedLabel() const;
    bool editing() const { return mode_ != Mode::Browse; }

    // A message that covers the menu until the next press.
    void showMessage(const std::string& title, const std::string& text);

private:
    enum class Mode { Browse, EditChoice, EditNumber, EditText, Message };
    struct Level
    {
        MenuItem* menu;
        int selected; // 0 is the "back" entry, 1.. are the children
    };

    MenuItem* current() const { return levels_.back().menu; }
    MenuItem* selectedItem() const;
    void click();
    void back();
    void turn(int steps);
    void leaveLevel();
    void renderList(Framebuffer& fb, const std::vector<std::pair<std::string, std::string>>& rows, int selected) const;
    static std::string valueText(const MenuItem& item);

    MenuItem root_;
    bool active_ = false;
    std::vector<Level> levels_;
    Mode mode_ = Mode::Browse;

    int editIndex_ = 0;       // EditChoice
    int editValue_ = 0;       // EditNumber
    int editOriginal_ = 0;
    std::string editText_;    // EditText
    int cursor_ = 0;
    std::string messageTitle_;
    std::string messageText_;
};

} // namespace fdve
