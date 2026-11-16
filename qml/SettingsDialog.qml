pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IgittStyle as Styles
import Cache

// Theme, fonts and canvas behaviour, and the accounts that tell more
// about links. Every setting takes effect at once.
Dialog {
    id: dialog

    title: qsTr("Settings")
    modal: true
    closeButton: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520

    ColumnLayout {
        width: parent.width
        spacing: Styles.Primitives.spacing.sp150

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("General") }
            TabButton { text: qsTr("Accounts") }
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: tabs.currentIndex

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Styles.Primitives.spacing.sp100

                Label {
                    text: qsTr("Appearance")
                    font.bold: true
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Styles.Primitives.spacing.sp150
                    rowSpacing: Styles.Primitives.spacing.sp100

                    Label { text: qsTr("Theme") }
                    // The system's light or dark, then every theme by name
                    ComboBox {
                        Layout.fillWidth: true
                        textRole: "name"
                        valueRole: "id"
                        model: [{id: "system", name: qsTr("System")}].concat(Styles.Themes.available)
                        currentIndex: count > 0 ? Math.max(0, indexOfValue(Styles.Themes.theme)) : -1
                        onActivated: Styles.Themes.theme = currentValue
                    }

                    Item { width: 1; height: 1 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.textMuted
                            font.pixelSize: Styles.Primitives.fontSize.small
                            text: qsTr("These are igitt's themes. Your own go in %1 as JSON files and are read again whenever they change.")
                                      .arg(Styles.Themes.folder)
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: Styles.Themes.errors.length > 0
                            wrapMode: Text.Wrap
                            color: Theme.s.danger_default
                            text: Styles.Themes.errors.join("\n")
                        }
                        Button {
                            text: qsTr("Open the theme folder")
                            styles: [Styles.ButtonStyle.small]
                            onClicked: Qt.openUrlExternally(Styles.Themes.openFolder())
                        }
                    }

                    Label { text: qsTr("Interface font") }
                    RowLayout {
                        TextField {
                            Layout.fillWidth: true
                            text: Theme.prefs.uiFamily
                            onEditingFinished: Theme.prefs.uiFamily = text
                        }
                        SpinBox {
                            from: 7
                            to: 20
                            value: Theme.prefs.uiSize
                            onValueModified: Theme.prefs.uiSize = value
                        }
                    }

                    Label { text: qsTr("Editor font") }
                    RowLayout {
                        TextField {
                            Layout.fillWidth: true
                            text: Theme.prefs.codeFamily
                            onEditingFinished: Theme.prefs.codeFamily = text
                        }
                        SpinBox {
                            from: 7
                            to: 20
                            value: Theme.prefs.codeSize
                            onValueModified: Theme.prefs.codeSize = value
                        }
                    }
                }

                MenuSeparator { Layout.fillWidth: true }

                Label {
                    text: qsTr("Canvas")
                    font.bold: true
                }
                Switch {
                    text: qsTr("The mouse wheel zooms (otherwise it scrolls; Ctrl+wheel always zooms)")
                    checked: Theme.prefs.wheelZooms
                    onToggled: Theme.prefs.wheelZooms = checked
                }
                Switch {
                    text: qsTr("Snap to other items (hold Space to move freely)")
                    checked: Theme.prefs.snap
                    onToggled: Theme.prefs.snap = checked
                }
                Switch {
                    text: qsTr("Snap to the dot grid")
                    checked: Theme.prefs.snapGrid
                    onToggled: Theme.prefs.snapGrid = checked
                }
                Switch {
                    text: qsTr("Show [[links]] between notes as dotted lines")
                    checked: Theme.prefs.showWikiLinks
                    onToggled: Theme.prefs.showWikiLinks = checked
                }
                Switch {
                    text: qsTr("Show the minimap")
                    checked: Theme.prefs.showMinimap
                    onToggled: Theme.prefs.showMinimap = checked
                }

                MenuSeparator { Layout.fillWidth: true }

                Label {
                    text: qsTr("History")
                    font.bold: true
                }
                Switch {
                    enabled: History.available
                    text: History.available ? qsTr("Keep versions of every project with git")
                                            : qsTr("Keep versions of every project (needs git, which isn't installed)")
                    checked: History.enabled && History.available
                    onToggled: History.enabled = checked
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Styles.Primitives.fontSize.small
                    text: qsTr("The workspace folder becomes a git repository of its own; a version is committed a little while after each change.")
                }
            }

            // Accounts on Jira and the like
            ColumnLayout {
                id: accounts
                Layout.fillWidth: true
                spacing: Styles.Primitives.spacing.sp100
                property bool busy: false
                property string message
                property bool failed: false

                Connections {
                    target: Links
                    function onAccountAdded(ok, message) {
                        accounts.busy = false
                        accounts.failed = !ok
                        accounts.message = message
                        if (ok) {
                            hostField.clear()
                            userField.clear()
                            tokenField.clear()
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Styles.Primitives.fontSize.small
                    text: qsTr("Links to Jira issues, Confluence pages, GitLab merge requests and pipelines, and Figma designs show their title, status and details while the pointer is on them. The token is kept in your system's keychain and is only ever sent to the sites it is for.")
                }
                Label {
                    Layout.fillWidth: true
                    visible: !Links.accounts.keychain
                    wrapMode: Text.Wrap
                    color: Theme.s.danger_default
                    text: qsTr("This build has no keychain to keep tokens in (on Linux it needs libsecret).")
                }

                Repeater {
                    model: Links.accounts.list
                    delegate: ColumnLayout {
                        id: account
                        required property var modelData
                        readonly property bool everySite: modelData.host === "*.atlassian.net"
                        Layout.fillWidth: true
                        spacing: Styles.Primitives.spacing.sp050
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Styles.Primitives.spacing.sp100
                            Label {
                                text: Links.providers.find(p => p.id === account.modelData.provider)?.name ?? account.modelData.provider
                                font.weight: Styles.Primitives.fontWeight.semiBold
                            }
                            Label {
                                Layout.fillWidth: true
                                text: account.everySite ? qsTr("every atlassian.net site") : account.modelData.host
                                elide: Text.ElideRight
                            }
                            Label {
                                text: account.modelData.user
                                color: Theme.textMuted
                                elide: Text.ElideRight
                                Layout.maximumWidth: 180
                            }
                            Button {
                                text: qsTr("Remove")
                                styles: [Styles.ButtonStyle.small]
                                onClicked: Links.accounts.remove(account.modelData.provider, account.modelData.host)
                            }
                        }
                        // Figma: whose files the command palette searches
                        RowLayout {
                            visible: account.modelData.provider === "figma"
                            Layout.fillWidth: true
                            spacing: Styles.Primitives.spacing.sp100
                            Label {
                                text: qsTr("Teams")
                                color: Theme.textMuted
                                font.pixelSize: Styles.Primitives.fontSize.small
                            }
                            TextField {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100 // fills the row; its text doesn't widen the dialog
                                text: (account.modelData.teams || []).join(", ")
                                placeholderText: qsTr("Team addresses (figma.com/files/team/…), for searching their files")
                                font.pixelSize: Styles.Primitives.fontSize.small
                                onEditingFinished: Links.accounts.setTeams(account.modelData.provider,
                                                                           account.modelData.host, text)
                            }
                        }
                        // Old addresses of the same site: their links are read from this host
                        RowLayout {
                            visible: account.modelData.provider === "jira"
                            Layout.fillWidth: true
                            spacing: Styles.Primitives.spacing.sp100
                            Label {
                                text: qsTr("Also for")
                                color: Theme.textMuted
                                font.pixelSize: Styles.Primitives.fontSize.small
                            }
                            TextField {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100 // fills the row; its text doesn't widen the dialog
                                text: (account.modelData.aliases || []).map(a => a.replace("=", " = ")).join(", ")
                                // Every site's account must say which site an old address moved to
                                placeholderText: account.everySite
                                                 ? qsTr("Old addresses and their sites: jira.example.com = example.atlassian.net")
                                                 : qsTr("Other addresses of this Jira, e.g. jira.example.com")
                                font.pixelSize: Styles.Primitives.fontSize.small
                                onEditingFinished: Links.accounts.setAliases(account.modelData.provider,
                                                                             account.modelData.host, text)
                            }
                        }
                    }
                }
                Label {
                    visible: Links.accounts.list.length === 0
                    text: qsTr("No accounts yet.")
                    color: Theme.textMuted
                }

                MenuSeparator { Layout.fillWidth: true }

                Label {
                    text: qsTr("Add an account")
                    font.bold: true
                }
                GridLayout {
                    Layout.fillWidth: true
                    enabled: Links.accounts.keychain && !accounts.busy
                    columns: 2
                    columnSpacing: Styles.Primitives.spacing.sp150
                    rowSpacing: Styles.Primitives.spacing.sp100

                    Label { text: qsTr("Service") }
                    ComboBox {
                        id: providerBox
                        // Figma's accounts are for figma.com: no host, no email
                        readonly property bool fixed: (Links.providers[currentIndex] || {}).fixedHost !== undefined
                                                      && Links.providers[currentIndex].fixedHost !== ""
                        // Atlassian Cloud's email; GitLab and Figma need only the token
                        readonly property bool needsUser: (Links.providers[currentIndex] || {}).needsUser === true
                        readonly property bool gitlab: currentValue === "gitlab"
                        Layout.fillWidth: true
                        textRole: "name"
                        valueRole: "id"
                        model: Links.providers
                    }

                    Label {
                        visible: !providerBox.fixed
                        text: qsTr("Host")
                    }
                    TextField {
                        id: hostField
                        visible: !providerBox.fixed
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100 // fills the row; its text doesn't widen the dialog
                        placeholderText: providerBox.gitlab ? qsTr("gitlab.com, or your GitLab's address")
                                                            : qsTr("example.atlassian.net, or an issue's address")
                    }

                    Item {
                        visible: everySiteBox.visible
                        width: 1
                        height: 1
                    }
                    CheckBox {
                        id: everySiteBox
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100 // fills the row; its text doesn't widen the dialog
                        visible: !providerBox.fixed && /\.atlassian\.net(?:[\/:]|$)/i.test(hostField.text.trim())
                        checked: true
                        text: qsTr("For every atlassian.net site: one API token works on all of them")
                    }

                    Label {
                        visible: providerBox.needsUser
                        text: qsTr("Email")
                    }
                    TextField {
                        id: userField
                        visible: providerBox.needsUser
                        Layout.fillWidth: true
                        placeholderText: qsTr("Atlassian Cloud only; empty for a personal access token")
                    }

                    Label { text: qsTr("Token") }
                    TextField {
                        id: tokenField
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100 // fills the row; its text doesn't widen the dialog
                        echoMode: TextInput.Password
                        placeholderText: providerBox.fixed ? qsTr("Personal access token")
                                                           : qsTr("API token or personal access token")
                        onAccepted: addButton.clicked()
                    }

                    Item { width: 1; height: 1 }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                        font.pixelSize: Styles.Primitives.fontSize.small
                        textFormat: Text.StyledText
                        linkColor: Theme.s.text_link
                        text: providerBox.fixed
                              ? qsTr("Figma: a personal access token, made in Figma under Settings, Security, with read access to file content.")
                              : providerBox.gitlab
                              ? qsTr("GitLab: a personal access token with the read_api scope, made under your avatar, Edit profile, Access tokens.")
                              : qsTr("Atlassian Cloud (Jira and Confluence): your email and an <a href=\"https://id.atlassian.com/manage-profile/security/api-tokens\">API token</a>. Data Center: a personal access token from your profile, without an email.")
                        onLinkActivated: (link) => Qt.openUrlExternally(link)
                        HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    }

                    Item { width: 1; height: 1 }
                    RowLayout {
                        spacing: Styles.Primitives.spacing.sp100
                        Button {
                            id: addButton
                            text: accounts.busy ? qsTr("Signing in…") : qsTr("Sign in and add")
                            enabled: (providerBox.fixed || hostField.text.trim() !== "") && tokenField.text.trim() !== ""
                            onClicked: {
                                if (!enabled)
                                    return
                                accounts.busy = true
                                accounts.message = ""
                                Links.addAccount(providerBox.currentValue, providerBox.fixed ? "" : hostField.text,
                                                 providerBox.needsUser ? userField.text : "", tokenField.text,
                                                 everySiteBox.visible && everySiteBox.checked)
                            }
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: accounts.message !== ""
                    wrapMode: Text.Wrap
                    color: accounts.failed ? Theme.s.danger_default : Theme.s.success_default
                    text: accounts.message
                }
            }
        }
    }
}
