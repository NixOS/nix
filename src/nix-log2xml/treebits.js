/* Acknowledgement: this is based on the Wikipedia table-of-contents
 * toggle. */


var idCounter = 0;


function showTreeToggle(isHidden) {
    if (document.getElementById) {
        var id = "toggle_" + idCounter;
        document.writeln(
            '<a href="javascript:toggleTree(\'' + id + '\')" class="toggle" id="' + id + '">' +
            '<span class="showTree" ' + (isHidden ? '' : 'style="display: none;"') + '>+</span>' +
            '<span class="hideTree" ' + (isHidden ? 'style="display: none;"' : '') + '>-</span>' +
            '</a>');
        idCounter = idCounter + 1;
    }
}


function toggleTree(id) {

    var href = document.getElementById(id);

    var node = href;
    var tree = null;
    while (node != null) {
        if (node.className == "nesting") tree = node;
        node = node.nextSibling;
    }

    node = href.firstChild;
    var hideTree = null;
    var showTree = null;
    while (node != null) {
        if (node.className == "showTree") showTree = node;
        else if (node.className == "hideTree") hideTree = node;
        node = node.nextSibling;
    }
    
    if (tree.style.display == 'none') {
        tree.style.display = '';
        hideTree.style.display = '';
        showTree.style.display = 'none';
    } else {
        tree.style.display = 'none';
        hideTree.style.display = 'none';
        showTree.style.display = '';
    }
}
