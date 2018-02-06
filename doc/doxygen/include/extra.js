$(document).ready(function() {
    $("div.headertitle").addClass("page-header");
    $("div.title").addClass("h1");

    $('li > a[href="index.html"] > span').before("<i class='fa fa-cog'></i> ");
    $('li > a[href="modules.html"] > span').before("<i class='fa fa-square'></i> ");
    $('li > a[href="namespaces.html"] > span').before("<i class='fa fa-bars'></i> ");
    $('li > a[href="annotated.html"] > span').before("<i class='fa fa-list-ul'></i> ");
    $('li > a[href="classes.html"] > span').before("<i class='fa fa-book'></i> ");
    $('li > a[href="inherits.html"] > span').before("<i class='fa fa-sitemap'></i> ");
    $('li > a[href="functions.html"] > span').before("<i class='fa fa-list'></i> ");
    $('li > a[href="functions_func.html"] > span').before("<i class='fa fa-list'></i> ");
    $('li > a[href="functions_vars.html"] > span').before("<i class='fa fa-list'></i> ");
    $('li > a[href="functions_enum.html"] > span').before("<i class='fa fa-list'></i> ");
    $('li > a[href="functions_eval.html"] > span').before("<i class='fa fa-list'></i> ");
    $('img[src="ftv2ns.png"]').replaceWith('<span class="label label-danger">N</span> ');
    $('img[src="ftv2cl.png"]').replaceWith('<span class="label label-danger">C</span> ');

    $("ul.tablist").addClass("nav nav-pills nav-justified");
    $("ul.tablist").css("margin-top", "0.5em");
    $("ul.tablist").css("margin-bottom", "0.5em");
    $("li.current").addClass("active");
    $("iframe").attr("scrolling", "yes");

    $("#nav-path > ul").addClass("breadcrumb");

    $("table.params").addClass("table");
    $("div.ingroups").wrapInner("<small></small>");
    $("div.levels").css("margin", "0.5em");
    $("div.levels > span").addClass("btn btn-default btn-xs");
    $("div.levels > span").css("margin-right", "0.25em");

    $("table.directory").addClass("table table-striped");
    $("div.summary > a").addClass("btn btn-default btn-xs");
    $("table.fieldtable").addClass("table table-striped table-bordered");
    $(".fragment").addClass("well");
    $(".memitem").addClass("panel panel-default");
    $(".memproto").addClass("panel-heading");
    $(".memdoc").addClass("panel-body");
    $("span.mlabel").addClass("label label-info");

    $("table.memberdecls").addClass("table table-responsive");
    $("[class^=memitem]").addClass("active");

    $("div.ah").addClass("btn btn-default");
    $("span.mlabels").addClass("pull-right");
    $("table.mlabels").css("width", "100%");
    $("td.mlabels-right").addClass("pull-right");

    $("div.ttc").addClass("panel panel-primary");
    $("div.ttname").addClass("panel-heading");
    $("div.ttname a").css("color", 'white');
    $("div.ttdef,div.ttdoc,div.ttdeci").addClass("panel-body");

    $('#MSearchBox').parent().remove();

    $('div.fragment.well div.line:first').css('margin-top', '15px');
    $('div.fragment.well div.line:last').css('margin-bottom', '15px');

    $('table.doxtable')
        .removeClass('doxtable')
        .addClass('table table-striped table-bordered')
        .each(function() {
            $(this).prepend('<thead></thead>');
            $(this).find('tbody > tr:first').prependTo($(this).find('thead'));
            $(this).find('td > span.success').parent().addClass('success');
            $(this).find('td > span.warning').parent().addClass('warning');
            $(this).find('td > span.danger').parent().addClass('danger');
        });

    if($('div.fragment.well div.ttc').length > 0) {
        $('div.fragment.well div.line:first')
            .parent()
            .removeClass('fragment well');
    }

    $('table.memberdecls').find('.memItemRight').each(function() {
        $(this).contents().appendTo($(this).siblings('.memItemLeft'));
        $(this).siblings('.memItemLeft').attr('align', 'left');
    });

    $('table.memberdecls')
        .has("a[name=\"enum-members\"]")
        .find("td.memItemLeft")
        .each(function() {
            $(this).contents().each(function() {
                if(this.nodeType === 3) {
                    if(this.textContent === "enum \u00A0") {
                        $(this).before("enum ");
                        $(this).remove();
                    } else if(this.textContent === " { ") {
                        $(this).before(" { \n");
                        $(this).remove();
                    } else if(this.textContent === "\n\u00A0\u00A0") {
                        $(this).remove();
                    } else if(this.textContent === "\n }") {
                        $(this).before("}");
                        $(this).remove();
                    }
                } else if($(this).is("br")) {
                    $(this).remove();
                } else if($(this).is("a")) {
                    $(this).before("  ");
                }
            });
        });

    function getOriginalWidthOfImg(img_el) {
        var t = new Image();
        t.src = false;
        t.src |= (img_el.getAttribute ? img_el.getAttribute("src") : false);
        t.src |= img_el.src;
        return t.width;
    }

    $('div.dyncontent').find('img').each(function() {
        if(getOriginalWidthOfImg($(this)[0]) > $('#content>div.container').width()) {
            $(this).css('width', '100%');
        }
    });

    $(".memitem").removeClass('memitem');
    $(".memproto").removeClass('memproto');
    $(".memdoc").removeClass('memdoc');
    $("span.mlabel").removeClass('mlabel');
    $("table.memberdecls").removeClass('memberdecls');
    $("[class^=memitem]").removeClass('memitem');
    $("span.mlabels").removeClass('mlabels');
    $("table.mlabels").removeClass('mlabels');
    $("td.mlabels-right").removeClass('mlabels-right');
    $("table.fieldtable").removeClass("fieldtable");
    $(".navpath").removeClass('navpath');
    $("li.navelem").removeClass('navelem');
    $("a.el").removeClass('el');
    $("div.ah").removeClass('ah');
    $("div.header").removeClass("header");

    $('.mdescLeft').each(function() {
        if($(this).html() == "&nbsp;") {
            $(this).siblings('.mdescRight').attr('colspan', 2);
            $(this).remove();
        }
    });

    $('td.memItemLeft').each(function() {
        if($(this).siblings('.memItemRight').html() == "") {
            $(this).attr('colspan', 2);
            $(this).siblings('.memItemRight').remove();
        }
        this.style.setProperty("white-space", "pre-wrap", "important");
    });
});
