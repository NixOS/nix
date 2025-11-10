"\\[\\]\\{#(?<anchor>[^\\}]+?)\\}" as $empty_anchor_regex |
"\\[(?<text>[^\\]]+?)\\]\\{#(?<anchor>[^\\}]+?)\\}" as $anchor_regex |


def transform_anchors_html:
    . | gsub($empty_anchor_regex; "<a id=\"" + .anchor + "\"></a>")
      | gsub($anchor_regex; "<a href=\"#" + .anchor + "\" id=\"" + .anchor + "\">" + .text + "</a>");


def transform_anchors_strip:
    . | gsub($empty_anchor_regex; "")
      | gsub($anchor_regex; .text);


def map_contents_recursively(transformer):
    . + {
        Chapter: (.Chapter + {
            content: .Chapter.content | transformer,
            sub_items: .Chapter.sub_items | map(map_contents_recursively(transformer)),
        }),
    };


def process_command:
    .[0] as $context |
    .[1] as $body |
    $body + {
        sections: $body.sections | map(map_contents_recursively(if $context.renderer == "html" then transform_anchors_html else transform_anchors_strip end)),
    };

process_command
