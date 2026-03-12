// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
//
// This file is part of DirectPipe.
//
// DirectPipe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DirectPipe is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DirectPipe. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file StreamDeckTab.cpp
 * @brief Stream Deck tab implementation — WebSocket/HTTP server status
 */

#include "StreamDeckTab.h"

namespace directpipe {

StreamDeckTab::StreamDeckTab(ControlManager& manager)
    : manager_(manager)
{
    // -- WebSocket section --
    wsSectionLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    wsSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsSectionLabel_);

    wsPortLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsPortLabel_);
    wsPortValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    wsPortValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(wsPortValueLabel_);

    wsStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsStatusLabel_);
    wsStatusValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(wsStatusValueLabel_);

    wsClientsLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsClientsLabel_);
    wsClientsValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    wsClientsValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(wsClientsValueLabel_);

    wsToggleButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour));
    wsToggleButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(kTextColour));
    wsToggleButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(kTextColour));
    wsToggleButton_.onClick = [this] {
        auto& ws = manager_.getWebSocketServer();
        if (ws.isRunning()) {
            ws.stop();
        } else {
            ws.start(ws.getPort());
        }
        updateStatus();
    };
    addAndMakeVisible(wsToggleButton_);

    // -- HTTP section --
    httpSectionLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    httpSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(httpSectionLabel_);

    httpPortLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(httpPortLabel_);
    httpPortValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    httpPortValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(httpPortValueLabel_);

    httpStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(httpStatusLabel_);
    httpStatusValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(httpStatusValueLabel_);

    httpToggleButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour));
    httpToggleButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(kTextColour));
    httpToggleButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(kTextColour));
    httpToggleButton_.onClick = [this] {
        auto& http = manager_.getHttpApiServer();
        if (http.isRunning()) {
            http.stop();
        } else {
            http.start(http.getPort());
        }
        updateStatus();
    };
    addAndMakeVisible(httpToggleButton_);

    // -- Info text --
    infoLabel_.setFont(juce::Font(11.0f));
    infoLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    infoLabel_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(infoLabel_);

    updateStatus();

    // Refresh status at 2 Hz
    startTimerHz(2);
}

StreamDeckTab::~StreamDeckTab()
{
    stopTimer();
}

void StreamDeckTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));

    // Separator between WebSocket and HTTP sections
    auto bounds = getLocalBounds().reduced(8);
    constexpr int rowH = 28;
    constexpr int gap  = 6;

    // After WS section: header + port + status + clients + button = 5 rows
    int separatorY = bounds.getY() + (rowH + gap) * 5 - gap / 2;
    g.setColour(juce::Colour(kDimTextColour).withAlpha(0.3f));
    g.drawHorizontalLine(separatorY, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));
}

void StreamDeckTab::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    constexpr int rowH   = 28;
    constexpr int gap    = 6;
    constexpr int labelW = 80;
    constexpr int valueW = 100;
    constexpr int btnW   = 70;

    int y = bounds.getY();

    // -- WebSocket section --
    wsSectionLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    wsPortLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    wsPortValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    wsStatusLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    wsStatusValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    wsClientsLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    wsClientsValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    wsToggleButton_.setBounds(bounds.getX(), y, btnW, rowH);
    y += rowH + gap + 8; // extra gap for separator

    // -- HTTP section --
    httpSectionLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    httpPortLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    httpPortValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    httpStatusLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    httpStatusValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    httpToggleButton_.setBounds(bounds.getX(), y, btnW, rowH);
    y += rowH + gap + 8;

    // Info text fills remaining space
    infoLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), bounds.getBottom() - y);
}

void StreamDeckTab::timerCallback()
{
    updateStatus();
}

void StreamDeckTab::updateStatus()
{
    auto& ws = manager_.getWebSocketServer();
    auto& http = manager_.getHttpApiServer();

    // WebSocket
    wsPortValueLabel_.setText(juce::String(ws.getPort()), juce::dontSendNotification);

    if (ws.isRunning()) {
        wsStatusValueLabel_.setText("Running", juce::dontSendNotification);
        wsStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kGreenColour));
        wsToggleButton_.setButtonText("Stop");
    } else {
        wsStatusValueLabel_.setText("Stopped", juce::dontSendNotification);
        wsStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kRedColour));
        wsToggleButton_.setButtonText("Start");
    }

    wsClientsValueLabel_.setText(juce::String(ws.getClientCount()), juce::dontSendNotification);

    // HTTP
    httpPortValueLabel_.setText(juce::String(http.getPort()), juce::dontSendNotification);

    if (http.isRunning()) {
        httpStatusValueLabel_.setText("Running", juce::dontSendNotification);
        httpStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kGreenColour));
        httpToggleButton_.setButtonText("Stop");
    } else {
        httpStatusValueLabel_.setText("Stopped", juce::dontSendNotification);
        httpStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kRedColour));
        httpToggleButton_.setButtonText("Start");
    }
}

} // namespace directpipe
