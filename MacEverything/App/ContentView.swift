import SwiftUI

struct ContentView: View {
    @StateObject private var viewModel = SearchViewModel()
    @ObservedObject private var searchOptions = SearchOptions.shared
    @State private var scrollViewID = 0
    @FocusState private var isSearchFieldFocused: Bool

    @ViewBuilder
    private var aiTranslationStatusBar: some View {
        if viewModel.aiModeEnabled {
            HStack(spacing: 4) {
                if viewModel.aiIsTranslating {
                    ProgressView()
                        .controlSize(.small)
                    if let partial = viewModel.aiTranslatedQuery {
                        Text(partial)
                            .font(.system(.caption, design: .monospaced))
                            .foregroundColor(.purple.opacity(0.6))
                            .lineLimit(1)
                    } else {
                        Text("Translating...")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                } else if let translated = viewModel.aiTranslatedQuery {
                    Image(systemName: "arrow.right")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                    Text(translated)
                        .font(.system(.caption, design: .monospaced))
                        .foregroundColor(.purple)
                        .lineLimit(1)
                        .textSelection(.enabled)
                } else if let error = viewModel.aiError {
                    Image(systemName: "exclamationmark.triangle")
                        .font(.caption2)
                        .foregroundColor(.orange)
                    Text(error)
                        .font(.caption)
                        .foregroundColor(.orange)
                        .lineLimit(1)
                }
                Spacer()
            }
            .padding(.horizontal, 8)
            .frame(height: 18)
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            // Permission banner
            PermissionView()

            // Search bar (Alfred-style)
            HStack(spacing: 12) {
                Image(systemName: "magnifyingglass")
                    .font(.system(size: 26, weight: .medium))
                    .foregroundColor(.blue)
                HighlightedSearchField(
                    text: $viewModel.searchText,
                    placeholder: "Search files... (infile: for content search)",
                    ghostSuggestion: viewModel.ghostSuggestion,
                    isFocused: $isSearchFieldFocused,
                    onTab: {
                        if viewModel.ghostSuggestion != nil {
                            viewModel.acceptGhostSuggestion()
                            return true
                        }
                        return false
                    }
                )
                .frame(height: 36)
                .onChange(of: viewModel.searchText) {
                    viewModel.onSearchTextChanged()
                }
                SearchOptionBadges(options: searchOptions)
                Button(action: {
                    viewModel.aiModeEnabled.toggle()
                    if viewModel.aiModeEnabled {
                        viewModel.checkAIServiceAvailability()
                    }
                }) {
                    Text("AI")
                        .font(.system(size: 11, weight: .semibold, design: .rounded))
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(viewModel.aiModeEnabled ? Color.purple : Color.gray.opacity(0.2))
                        .foregroundColor(viewModel.aiModeEnabled ? .white : .secondary)
                        .cornerRadius(4)
                }
                .buttonStyle(.plain)
                .help("Toggle AI search mode")
                if !viewModel.searchText.isEmpty {
                    Button {
                        viewModel.searchText = ""
                        // H-8: .onChange(of: searchText) will trigger onSearchTextChanged() automatically
                    } label: {
                        Image(systemName: "xmark.circle.fill")
                            .font(.system(size: 18))
                            .foregroundColor(.secondary)
                    }
                    .buttonStyle(.plain)
                    .accessibilityIdentifier("clearButton")
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 16)
            .background(.ultraThinMaterial)
            .cornerRadius(10)
            .overlay(
                RoundedRectangle(cornerRadius: 10)
                    .stroke(Color.blue, lineWidth: 2)
            )
            .padding(.horizontal, 8)
            .padding(.top, 8)

            aiTranslationStatusBar

            Divider()

            // Status bar
            HStack {
                if viewModel.isScanning {
                    ProgressView()
                        .controlSize(.small)
                    Text("Scanning... \(viewModel.scannedCount) items scanned")
                        .foregroundColor(.secondary)
                } else if viewModel.scanComplete {
                    if viewModel.isSyncing || viewModel.isBuildingIndex {
                        ProgressView()
                            .controlSize(.small)
                        Text(viewModel.isSyncing && viewModel.isBuildingIndex
                             ? "Syncing & building index..."
                             : viewModel.isBuildingIndex
                             ? "Building index..."
                             : "Syncing...")
                            .foregroundColor(.orange)
                    } else if viewModel.isMonitoring {
                        Circle()
                            .fill(.green)
                            .frame(width: 6, height: 6)
                        Text("Live")
                            .foregroundColor(.green)
                            .fontWeight(.medium)
                    }
                    Text("\(viewModel.totalRecords) files indexed")
                        .foregroundColor(.secondary)
                        .accessibilityIdentifier("indexedCount")
                    if viewModel.isContentIndexing, let progress = viewModel.contentIndexProgress {
                        Text("·")
                            .foregroundColor(.secondary)
                        ProgressView()
                            .controlSize(.small)
                        Text("Content indexing \(progress.indexed)/\(progress.total)")
                            .foregroundColor(.orange)
                    }
                    if viewModel.totalMatches > 0 {
                        Text("·")
                            .foregroundColor(.secondary)
                        Text("\(viewModel.totalMatches) matches")
                            .foregroundColor(.secondary)
                            .accessibilityIdentifier("matchCount")
                        Text("·")
                            .foregroundColor(.secondary)
                        Text(String(format: "%.1fms", viewModel.queryTimeMs))
                            .foregroundColor(.secondary)
                    }
                }
                Spacer()
            }
            .font(.callout)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(Color(nsColor: .controlBackgroundColor))
            .accessibilityIdentifier("statusBar")

            Divider()

            // Results list
            if viewModel.isScanning {
                VStack(spacing: 12) {
                    Spacer()
                    ProgressView()
                        .controlSize(.large)
                    Text("Indexing files... \(viewModel.scannedCount) items scanned")
                        .font(.callout)
                        .foregroundColor(.secondary)
                    Spacer()
                }
            } else if viewModel.isContentSearch {
                // Content search results
                if viewModel.contentResults.isEmpty && !viewModel.contentKeyword.isEmpty && viewModel.scanComplete {
                    VStack(spacing: 8) {
                        Spacer()
                        if viewModel.isContentIndexing {
                            ProgressView()
                                .controlSize(.large)
                                .padding(.bottom, 4)
                            Text("Content index is building...")
                                .font(.headline)
                                .foregroundColor(.orange)
                            if let progress = viewModel.contentIndexProgress {
                                Text("Indexed \(progress.indexed) / \(progress.total) files")
                                    .font(.subheadline)
                                    .foregroundColor(.secondary)
                            }
                            Text("Search results will appear after indexing completes")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        } else {
                            Image(systemName: "doc.text.magnifyingglass")
                                .font(.system(size: 36))
                                .foregroundColor(.secondary.opacity(0.5))
                                .padding(.bottom, 4)
                            Text("No content matches found")
                                .foregroundColor(.secondary)
                            if viewModel.contentIndexedCount == 0 {
                                Text("No files indexed. Configure extensions in Content Settings.")
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }
                        }
                        Spacer()
                    }
                } else if viewModel.contentKeyword.isEmpty {
                    VStack {
                        Spacer()
                        Text("Type a keyword after infile: to search file contents")
                            .foregroundColor(.secondary)
                        Spacer()
                    }
                } else {
                    ScrollView {
                        LazyVStack(spacing: 0) {
                            ForEach(viewModel.contentResults) { item in
                                ContentResultRow(item: item, keyword: viewModel.contentKeyword)
                                    .padding(.horizontal, 8)
                                    .padding(.vertical, 2)
                            }
                        }
                    }
                    .id(scrollViewID)
                    .accessibilityIdentifier("contentResultsList")

                    if viewModel.totalMatches > 0 {
                        HStack {
                            Spacer()
                            Text("\(viewModel.contentResults.count) content matches")
                                .font(.callout)
                                .foregroundColor(.secondary)
                                .padding(8)
                            Spacer()
                        }
                        .background(Color(nsColor: .controlBackgroundColor))
                    }
                }
            } else if viewModel.displayItems.isEmpty && !viewModel.searchText.isEmpty && viewModel.scanComplete {
                VStack {
                    Spacer()
                    Text("No results found")
                        .foregroundColor(.secondary)
                        .accessibilityIdentifier("noResultsLabel")
                    Spacer()
                }
            } else {
                ScrollView {
                    LazyVStack(spacing: 0) {
                        if viewModel.showingRecent && !viewModel.displayItems.isEmpty {
                            HStack {
                                HStack(spacing: 4) {
                                    Image(systemName: "clock")
                                    Text("Recent Files")
                                        .font(.callout)
                                        .fontWeight(.medium)
                                }
                                .foregroundColor(.white)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 3)
                                .background(
                                    RoundedRectangle(cornerRadius: 5)
                                        .fill(Color.orange)
                                )
                                Spacer()
                            }
                            .padding(.horizontal, 12)
                            .padding(.vertical, 4)
                        }
                        ForEach(viewModel.displayItems) { item in
                            ResultRow(item: item, hints: viewModel.highlightHints)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 2)
                                .id(item.id)
                        }
                        if viewModel.hasMoreResults {
                            HStack {
                                Spacer()
                                ProgressView()
                                    .controlSize(.small)
                                Text("Loading more results...")
                                    .font(.callout)
                                    .foregroundColor(.secondary)
                                Spacer()
                            }
                            .padding(.vertical, 8)
                            .onAppear {
                                viewModel.loadMore()
                            }
                        }
                    }
                }
                .id(scrollViewID)
                .accessibilityIdentifier("fileResultsList")
                .animation(.easeInOut(duration: 0.25), value: viewModel.showingRecent)

                if viewModel.totalMatches > 0 {
                    HStack {
                        Spacer()
                        Text("Showing \(viewModel.displayItems.count) of \(viewModel.totalMatches) results")
                            .font(.callout)
                            .foregroundColor(.secondary)
                            .padding(8)
                        Spacer()
                    }
                    .background(Color(nsColor: .controlBackgroundColor))
                }
            }
        }
        .frame(minWidth: 600, minHeight: 400)
        .onReceive(NotificationCenter.default.publisher(for: .rebuildIndex)) { _ in
            viewModel.rebuildIndex()
        }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)) { _ in
            viewModel.onWindowFocusChanged(true)
            isSearchFieldFocused = true
        }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didResignActiveNotification)) { _ in
            viewModel.onWindowFocusChanged(false)
        }
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didDeminiaturizeNotification)) { _ in
            scrollViewID += 1
        }
    }
}
