from maceverything_ai.prompt import build_messages, FEW_SHOT_EXAMPLES


def test_build_messages_structure():
    messages = build_messages("最近的PDF")
    assert messages[0]["role"] == "system"
    assert "ext:" in messages[0]["content"]
    expected_len = 1 + 2 * len(FEW_SHOT_EXAMPLES) + 1
    assert len(messages) == expected_len
    assert messages[-1] == {"role": "user", "content": "最近的PDF"}


def test_few_shot_examples_are_valid_queries():
    known_prefixes = [
        "ext:", "size:", "path:", "nopath:", "dm:", "dc:", "file:", "folder:",
        "content:", "regex:", "ww:", "parent:", "depth:", "pic:", "video:",
        "audio:", "doc:", "zip:", "exe:", "case:",
    ]
    for nl, query in FEW_SHOT_EXAMPLES:
        has_filter = any(p in query for p in known_prefixes)
        has_keyword = len(query.split()) > 0
        assert has_filter or has_keyword, f"Example '{nl}' → '{query}' has no filter or keyword"
