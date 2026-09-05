#pragma once

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <variant>
#include <algorithm>
#include <functional>
#include <string_view>

namespace Ashes {

struct GenericMenuParams;

//==============================================================================
// GenericMenuItem: Separator, Button, ButtonGroup, PopupButton
//==============================================================================

inline static const std::string kGenericMenuSeparatorText(1, '\0');

struct GenericMenuButtonParams
{
    std::string           text;
    std::function<bool()> is_checked;
    std::function<void()> on_clicked;
};

struct GenericMenuButtonGroupParams
{
    std::vector<std::string> texts;
    std::function<int()>     get_selection;
    std::function<void(int)> on_clicked;
};

struct GenericMenuPopupButtonParams
{
    std::string                        text;
    std::shared_ptr<GenericMenuParams> menu_params;
};

struct GenericMenuItemParamsVariant : std::variant<
    GenericMenuButtonParams,
    GenericMenuButtonGroupParams,
    GenericMenuPopupButtonParams>
{
    template <typename T> auto As()       { return std::get_if<T>(this); }
    template <typename T> auto As() const { return std::get_if<T>(this); }
};

//==============================================================================
// GenericMenu
//==============================================================================

struct GenericMenuParams
{
    std::pair<const GenericMenuItemParamsVariant*, int> FindItem(int pos) const
    {
        int cur = 0;
        for (const GenericMenuItemParamsVariant& item : items)
        {
            const auto* btns = std::get_if<GenericMenuButtonGroupParams>(&item);
            int n = (btns == nullptr ? 1 : static_cast<int>(btns->texts.size()));
            if (cur <= pos && pos < cur + n) { return {&item, pos - cur}; }
            cur += n;
        }
        return {nullptr, -1};
    }

    void AppendSeparator()
    {
        AppendButton(kGenericMenuSeparatorText, {}, {});
    }

    void AppendButton(
        std::string text,
        std::function<bool()> is_checked,
        std::function<void()> on_clicked)
    {
        if (!is_checked) { is_checked = [](){ return false; }; }
        if (!on_clicked) { on_clicked = [](){}; }
        auto& item = items.emplace_back().emplace<GenericMenuButtonParams>();
        item.text = std::move(text);
        item.is_checked = std::move(is_checked);
        item.on_clicked = std::move(on_clicked);
    }

    void AppendButtonGroup(
        std::vector<std::string> texts,
        std::function<int()> get_selection,
        std::function<void(int)> on_clicked)
    {
        if (!get_selection) { get_selection = [](){ return -1; }; }
        if (!on_clicked)    { on_clicked = [](int){}; }
        auto& item = items.emplace_back().emplace<GenericMenuButtonGroupParams>();
        item.texts = std::move(texts);
        item.get_selection = std::move(get_selection);
        item.on_clicked = std::move(on_clicked);
    }

    template <typename T>
    void AppendButtonGroup(
        std::vector<std::string> names,
        std::vector<T> options,
        std::function<T()> get_option,
        std::function<void(T)> set_option)
    {
        auto data = std::make_shared<std::vector<T>>(std::move(options));
        auto& item = items.emplace_back().emplace<GenericMenuButtonGroupParams>();
        item.texts = std::move(names);
        item.get_selection = [get_option = std::move(get_option), data]() {
            auto first = data->begin(), last = data->end();
            auto iter = std::find(first, last, get_option());
            return iter == last ? -1 : static_cast<int>(iter - first); };
        item.on_clicked = [set_option = std::move(set_option), data](int idx) {
            if (idx != -1) { set_option((*data)[idx]); } };
    }

    template <typename T>
    void AppendButtonGroup(
        std::initializer_list<std::pair<std::string_view, T>> named_options,
        std::function<T()> get_option,
        std::function<void(T)> set_option)
    {
        std::vector<std::string> names;
        std::vector<T> options;
        names.reserve(named_options.size());
        options.reserve(named_options.size());

        for (auto&& [name, option] : named_options)
        {
            names.push_back(std::string(name));
            options.push_back(option);
        }

        AppendButtonGroup(std::move(names), std::move(options),
            std::move(get_option), std::move(set_option));
    }

    template <typename T>
    void AppendButtonGroup(
        std::vector<std::pair<std::string, T>> named_options,
        std::function<T()> get_option,
        std::function<void(T)> set_option)
    {
        std::vector<std::string> names;
        std::vector<T> options;
        names.reserve(named_options.size());
        options.reserve(named_options.size());

        for (auto&& [name, option] : named_options)
        {
            names.push_back(std::move(name));
            options.push_back(std::move(option));
        }

        AppendButtonGroup(std::move(names), std::move(options),
            std::move(get_option), std::move(set_option));
    }

    std::shared_ptr<GenericMenuParams> AppendPopupButton(std::string text)
    {
        auto& item = items.emplace_back().emplace<GenericMenuPopupButtonParams>();
        item.text = std::move(text);
        item.menu_params = std::make_shared<GenericMenuParams>();
        return item.menu_params;
    }

    std::vector<GenericMenuItemParamsVariant> items;
};

}