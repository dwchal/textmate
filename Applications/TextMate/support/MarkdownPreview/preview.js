(function () {
	"use strict";

	// Scroll so the caret’s position in the source is visible: find the rendered
	// heading that starts the caret’s section and interpolate within the section.
	function scrollToCaret () {
		if(!window.TMCaret)
			return;

		var c = window.TMCaret;
		var headings = document.body.querySelectorAll("h1, h2, h3, h4, h5, h6");
		var pageEnd = document.documentElement.scrollHeight;
		var top    = c.section > 0 && headings[c.section - 1] ? headings[c.section - 1].offsetTop : 0;
		var bottom = headings[c.section] ? headings[c.section].offsetTop : pageEnd;
		var fraction = c.end > c.start ? Math.min(1, Math.max(0, (c.line - c.start) / (c.end - c.start))) : 0;
		var y = top + fraction * (bottom - top);

		var marker = document.getElementById("tm-caret-marker");
		if(!marker)
		{
			marker = document.createElement("div");
			marker.id = "tm-caret-marker";
			document.body.appendChild(marker);
		}
		marker.style.top = y + "px";

		window.scrollTo(0, Math.max(0, y - window.innerHeight / 3));
	}

	function renderMermaid (done) {
		var blocks = document.querySelectorAll("pre > code.mermaid, pre > code.language-mermaid");
		if(!window.mermaid || blocks.length === 0)
			return done();

		for(var i = 0; i < blocks.length; ++i)
		{
			var div = document.createElement("div");
			div.className = "mermaid";
			div.textContent = blocks[i].textContent;
			blocks[i].parentNode.replaceWith(div);
		}

		var dark = window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches;
		try {
			mermaid.initialize({ startOnLoad: false, theme: dark ? "dark" : "default", securityLevel: "strict" });
			mermaid.run({ querySelector: "div.mermaid" }).then(done, done);
		} catch(e) {
			done();
		}
	}

	// Restore the scroll position at once (so a refresh doesn’t flash at the top),
	// then again after diagrams and images have changed the layout.
	scrollToCaret();
	renderMermaid(scrollToCaret);
	window.addEventListener("load", scrollToCaret);
})();
