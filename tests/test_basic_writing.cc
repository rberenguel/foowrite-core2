#include <gtest/gtest.h>
#include "../main/editor.h"
#include "../main/editor_mode.h"
#include "../main/keymap.h"
#include "./editor_stubs.hpp"

TEST(FooWrite, BasicWriting_Hello) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "ihelloU");
    auto result = editor.GetCurrentLine();
    auto count = editor.CountLines();
    EXPECT_EQ(result, "hello");
    EXPECT_EQ(count, 1);
}

TEST(FooWrite, BasicWriting_Hello2) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "ihello\n");
    auto line = editor.GetCurrentLine();
    auto doc = editor.GetDocument();
    auto count = editor.CountLines();
    EXPECT_EQ(line, "");
    EXPECT_EQ(doc, "hello\n\n");
    EXPECT_EQ(count, 2);
}

TEST(FooWrite, BasicWriting_Hello3) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "ihello\nworld");
    auto line = editor.GetCurrentLine();
    auto doc = editor.GetDocument();
    auto count = editor.CountLines();
    EXPECT_EQ(line, "world");
    EXPECT_EQ(doc, "hello\n\n");
    EXPECT_EQ(count, 2);
}

TEST(FooWrite, BasicWriting_NewLineAtEnd) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "ifoo\nbar\n");
    auto result = editor.GetDocument();
    auto count = editor.CountLines();
    EXPECT_EQ(count, 3);
    EXPECT_EQ(result, "foo\nbar\n\n");
}

TEST(FooWrite, BasicWriting_NewLineAtMid) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "ibarL\nsUpq");
    auto line = editor.GetCurrentLine();
    auto doc = editor.GetDocument();
    auto count = editor.CountLines();
    EXPECT_EQ(count, 2);
    EXPECT_EQ(line, "bpqa");
    EXPECT_EQ(doc, "ba\nsr\n");
}

TEST(FooWrite, BasicWriting_NewLineMultiple) {
    Output out;
    Editor editor;
    editor.Init(&out);
    SendString(&editor, "ia\n\nb\ncUUdU");
    auto doc = editor.GetDocument();
    auto line = editor.GetCurrentLine();
    auto count = editor.CountLines();
    EXPECT_EQ(count, 4);
    EXPECT_EQ(line, "a");
    EXPECT_EQ(doc, "a\nd\nb\nc\n");
}

TEST(FooWrite, BasicWriting_Backspace) {
    Output out;
    Editor editor;
    editor.Init(&out);
    auto mods = KeyModifiers{};
    SendString(&editor, "ithis\b");
    auto result = editor.GetCurrentLine();
    EXPECT_EQ(result, "thi");
    editor.ProcessKey(KEY_BACKSPACE, &mods, false);
    result = editor.GetCurrentLine();
    EXPECT_EQ(result, "th");
    editor.ProcessKey(KEY_BACKSPACE, &mods, false);
    result = editor.GetCurrentLine();
    EXPECT_EQ(result, "t");
    editor.ProcessKey(KEY_BACKSPACE, &mods, false);
    result = editor.GetCurrentLine();
    EXPECT_EQ(result, "");
}
