#ifndef CSETTINGSEDITOR_H
#define CSETTINGSEDITOR_H
/*
 * Copyright (c) 2020 Valve Corporation
 * Copyright (c) 2020 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This class takes a pointer to a widget and a list of layer settings
 * and creates a gui for displaying and editing those settings.
 *
 * Author: Richard S. Wright Jr. <richard@lunarg.com>
 */


#include <QScrollArea>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

#include <layerfile.h>
#include <khronossettings.h>
#include <khronossettingsadvanced.h>

#define     EDITOR_TYPE_GENERAL             0
#define     EDITOR_TYPE_KHRONOS             1
#define     EDITOR_TYPE_kHRONOS_ADVANCED    2

class CSettingsEditor : public QObject
    {
    Q_OBJECT

public:
    CSettingsEditor();

    // Creates controls and sets up any signals
    void CreateGUI(QScrollArea *pDestination, QVector<TLayerSettings *>& layerSettings, int editorType, QString qsMessage);
    void CleanupGUI(void);                  // Clears all the controls and deletes edit area
    bool CollectSettings(void);             // Transfer controls to layerSettings.
                                            // Returns false if no settings were changed

    // Allow disabling and re-enabling all the edit controls
    void SetEnabled(bool bEnable);

protected:
    // Every edit control has one of these
    QVector<QWidget *> prompts;             // Just QLabels...

    // Actual edit controls, there may be more than one control per settings
    // entry, and so settings fields may have duplicates as these two mirror
    // each other exactly.
    QVector<QWidget *> inputControls;
    QVector<TLayerSettings *> linkedSetting;

    QWidget                 *pEditArea;
    KhronosSettings         *pKhronosEditor;
    KhronosSettingsAdvanced *pKhronosEditorAdvanced;


    ///////////////////////////////////////////////////////////////////
    // This might not be a good idea... but I think it's okay. There
    // is only ever one button and one edit field that goes with it.
    // This could be more general and I have an idea how to make it
    // work, but for now this is cheap and cheerful and will work.
    QLineEdit     *pButtonBuddy;
    QPushButton   *pBrowseButton;

    // When disabled, we hide the controls, but display a label
    // that says the controls are hiddent because the layer
    // is not enabled.
    bool        bEnabled;
    QLabel      *pEnabledPrompt;

    QScrollArea *pScrollArea;

public Q_SLOTS:
    void browseFileButtonPressed(void);
    void browseFolderButtonPressed(void);
    };

#endif // CSETTINGSEDITOR_H