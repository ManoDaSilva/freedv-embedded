#include "ui/Menu.h"

#include <algorithm>

namespace fdve {

namespace {

constexpr int kBodyTop = 18;   // first list row; row 16 stays empty
constexpr int kRowHeight = 9;
constexpr int kVisibleRows = 5;
constexpr int kCharsPerRow = 21;

// Characters offered by the text editor. The blank comes first: pressing on a
// blank ends the entry.
const std::string kCharset = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/-";

void header(Framebuffer& fb, const std::string& title)
{
    if (Framebuffer::textWidth(title, 2) <= Framebuffer::Width) fb.text(0, 1, title, 2);
    else fb.text(0, 4, title.substr(0, kCharsPerRow));
}

void centred(Framebuffer& fb, int y, const std::string& text, int scale)
{
    fb.text((Framebuffer::Width - Framebuffer::textWidth(text, scale)) / 2, y, text, scale);
}

} // namespace

Menu::Menu(MenuItem root) : root_(std::move(root)) {}

void Menu::setRoot(MenuItem root)
{
    root_ = std::move(root);
    close();
}

void Menu::open()
{
    active_ = true;
    mode_ = Mode::Browse;
    levels_.clear();
    levels_.push_back({&root_, 1});
    if (root_.children.empty()) levels_.back().selected = 0;
}

void Menu::close()
{
    while (levels_.size() > 1) leaveLevel();
    levels_.clear();
    active_ = false;
    mode_ = Mode::Browse;
}

void Menu::showMessage(const std::string& title, const std::string& text)
{
    if (!active_) open();
    messageTitle_ = title;
    messageText_ = text;
    mode_ = Mode::Message;
}

MenuItem* Menu::selectedItem() const
{
    const int index = levels_.back().selected;
    return index == 0 ? nullptr : &current()->children[index - 1];
}

std::string Menu::selectedLabel() const
{
    if (!active_ || levels_.empty()) return "";
    const MenuItem* item = selectedItem();
    return item == nullptr ? "" : item->label;
}

void Menu::leaveLevel()
{
    MenuItem* left = current();
    levels_.pop_back();
    if (left->onLeave) left->onLeave();
}

bool Menu::handle(const MenuEvents& events)
{
    if (!active_ || !events.any()) return false;
    if (events.steps != 0) turn(events.steps);
    for (int i = 0; i < events.clicks && active_; i++) click();
    for (int i = 0; i < events.backs && active_; i++) back();
    return true;
}

void Menu::turn(int steps)
{
    switch (mode_)
    {
        case Mode::Browse:
        {
            int& selected = levels_.back().selected;
            selected = std::clamp(selected + steps, 0, static_cast<int>(current()->children.size()));
            break;
        }
        case Mode::EditChoice:
        {
            const int count = static_cast<int>(selectedItem()->choices.size());
            editIndex_ = std::clamp(editIndex_ + steps, 0, std::max(0, count - 1));
            break;
        }
        case Mode::EditNumber:
        {
            MenuItem* item = selectedItem();
            editValue_ = std::clamp(editValue_ + steps * item->step, item->min, item->max);
            if (item->live && item->setInt) item->setInt(editValue_);
            break;
        }
        case Mode::EditText:
        {
            if (static_cast<int>(editText_.size()) <= cursor_) editText_.resize(cursor_ + 1, ' ');
            size_t pos = kCharset.find(editText_[cursor_]);
            if (pos == std::string::npos) pos = 0;
            const int count = static_cast<int>(kCharset.size());
            editText_[cursor_] = kCharset[((static_cast<int>(pos) + steps) % count + count) % count];
            break;
        }
        case Mode::Message:
            break;
    }
}

void Menu::click()
{
    switch (mode_)
    {
        case Mode::Browse:
        {
            MenuItem* item = selectedItem();
            if (item == nullptr)
            {
                back();
                return;
            }
            switch (item->kind)
            {
                case MenuItem::Kind::Submenu:
                    levels_.push_back({item, item->children.empty() ? 0 : 1});
                    break;
                case MenuItem::Kind::Choice:
                {
                    const std::string now = item->get ? item->get() : "";
                    const auto it = std::find(item->choices.begin(), item->choices.end(), now);
                    editIndex_ = it == item->choices.end() ? 0 : static_cast<int>(it - item->choices.begin());
                    if (!item->choices.empty()) mode_ = Mode::EditChoice;
                    break;
                }
                case MenuItem::Kind::Number:
                    editValue_ = editOriginal_ = item->getInt ? item->getInt() : item->min;
                    mode_ = Mode::EditNumber;
                    break;
                case MenuItem::Kind::Text:
                    editText_ = item->get ? item->get() : "";
                    cursor_ = 0;
                    mode_ = Mode::EditText;
                    break;
                case MenuItem::Kind::Action:
                    if (item->action) item->action();
                    break;
            }
            break;
        }
        case Mode::EditChoice:
        {
            MenuItem* item = selectedItem();
            if (item->set) item->set(item->choices[editIndex_]);
            mode_ = Mode::Browse;
            break;
        }
        case Mode::EditNumber:
        {
            MenuItem* item = selectedItem();
            if (item->setInt) item->setInt(editValue_);
            mode_ = Mode::Browse;
            break;
        }
        case Mode::EditText:
        {
            MenuItem* item = selectedItem();
            const bool onBlank = static_cast<int>(editText_.size()) <= cursor_ || editText_[cursor_] == ' ';
            if (!onBlank && cursor_ + 1 < item->maxLength)
            {
                cursor_++;
                break;
            }
            // A press on a blank, or on the last position, ends the entry.
            std::string result = editText_.substr(0, onBlank ? cursor_ : cursor_ + 1);
            if (item->set) item->set(result);
            mode_ = Mode::Browse;
            break;
        }
        case Mode::Message:
            mode_ = Mode::Browse;
            break;
    }
}

void Menu::back()
{
    switch (mode_)
    {
        case Mode::Browse:
            if (levels_.size() > 1) leaveLevel();
            else close();
            break;
        case Mode::EditNumber:
        {
            // Cancel: a live value was already applied, so put the old one back.
            MenuItem* item = selectedItem();
            if (item->live && item->setInt) item->setInt(editOriginal_);
            mode_ = Mode::Browse;
            break;
        }
        case Mode::EditChoice:
        case Mode::EditText:
        case Mode::Message:
            mode_ = Mode::Browse;
            break;
    }
}

std::string Menu::valueText(const MenuItem& item)
{
    switch (item.kind)
    {
        case MenuItem::Kind::Submenu: return ">";
        case MenuItem::Kind::Number: return item.getInt ? std::to_string(item.getInt()) + item.unit : "";
        case MenuItem::Kind::Choice:
        {
            const std::string now = item.get ? item.get() : "";
            for (size_t i = 0; i < item.choices.size() && i < item.choiceLabels.size(); i++)
                if (item.choices[i] == now) return item.choiceLabels[i];
            return now;
        }
        case MenuItem::Kind::Text: return item.get ? item.get() : "";
        case MenuItem::Kind::Action: return "";
    }
    return "";
}

void Menu::renderList(Framebuffer& fb, const std::vector<std::pair<std::string, std::string>>& rows, int selected) const
{
    const int count = static_cast<int>(rows.size());
    const int top = std::clamp(selected - kVisibleRows / 2, 0, std::max(0, count - kVisibleRows));
    for (int i = top; i < count && i < top + kVisibleRows; i++)
    {
        const int y = kBodyTop + (i - top) * kRowHeight;
        const bool isSelected = i == selected;
        if (isSelected) fb.fillRect(0, y - 1, Framebuffer::Width, kRowHeight);

        std::string value = rows[i].second;
        if (static_cast<int>(value.size()) > 12) value = value.substr(0, 12);
        std::string label = rows[i].first;
        const int room = kCharsPerRow - static_cast<int>(value.size()) - (value.empty() ? 0 : 1);
        if (static_cast<int>(label.size()) > room) label = label.substr(0, std::max(0, room));

        fb.text(2, y, label, 1, !isSelected);
        fb.text(Framebuffer::Width - 2 - Framebuffer::textWidth(value), y, value, 1, !isSelected);
    }
}

void Menu::render(Framebuffer& fb) const
{
    fb.clear();
    if (!active_) return;

    if (mode_ == Mode::Message)
    {
        header(fb, messageTitle_);
        // Wrap at word boundaries where possible.
        std::string rest = messageText_;
        for (int line = 0; line < kVisibleRows && !rest.empty(); line++)
        {
            size_t cut = std::min<size_t>(rest.size(), kCharsPerRow);
            if (cut < rest.size())
            {
                const size_t space = rest.rfind(' ', cut);
                if (space != std::string::npos && space > 0) cut = space;
            }
            fb.text(0, kBodyTop + line * kRowHeight, rest.substr(0, cut));
            rest = rest.substr(std::min(rest.size(), cut + (cut < rest.size() && rest[cut] == ' ' ? 1 : 0)));
        }
        return;
    }

    const MenuItem* item = selectedItem();
    switch (mode_)
    {
        case Mode::Browse:
        {
            header(fb, current()->label);
            std::vector<std::pair<std::string, std::string>> rows;
            rows.push_back({levels_.size() > 1 ? "< Back" : "< Exit", ""});
            for (const auto& child : current()->children) rows.push_back({child.label, valueText(child)});
            renderList(fb, rows, levels_.back().selected);
            break;
        }
        case Mode::EditChoice:
        {
            header(fb, item->label);
            std::vector<std::pair<std::string, std::string>> rows;
            for (size_t i = 0; i < item->choices.size(); i++)
                rows.push_back({i < item->choiceLabels.size() ? item->choiceLabels[i] : item->choices[i], ""});
            renderList(fb, rows, editIndex_);
            break;
        }
        case Mode::EditNumber:
        {
            header(fb, item->label);
            const std::string value = std::to_string(editValue_);
            const int width = Framebuffer::textWidth(value, 3);
            const int unitWidth = item->unit.empty() ? 0 : Framebuffer::textWidth(item->unit) + 4;
            const int x = (Framebuffer::Width - width - unitWidth) / 2;
            fb.text(x, 18, value, 3);
            fb.text(x + width + 4, 32, item->unit);
            // Position within the allowed range.
            fb.rect(0, 46, Framebuffer::Width, 12);
            const int span = std::max(1, item->max - item->min);
            fb.fillRect(2, 48, (editValue_ - item->min) * (Framebuffer::Width - 4) / span, 8);
            break;
        }
        case Mode::EditText:
        {
            header(fb, item->label);
            std::string shown = editText_;
            if (static_cast<int>(shown.size()) <= cursor_) shown.resize(cursor_ + 1, ' ');
            const int x = (Framebuffer::Width - Framebuffer::textWidth(shown, 2)) / 2;
            fb.text(x, 20, shown, 2);
            fb.fillRect(x + cursor_ * 12, 36, 10, 2); // cursor
            fb.text(0, 46, "turn: letter");
            fb.text(0, 55, "press: next/blank=end");
            break;
        }
        case Mode::Message:
            break;
    }
}

} // namespace fdve
