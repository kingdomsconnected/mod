/**
 * The texts of the `/book` demo, in the book movie's own markup.
 *
 * The chronicle tells the story of Kingdoms Connected as a monk of the
 * fifteenth century might have set it down, from what kingdomsconnected.com
 * and its changelog record. Each string is one page; the movie breaks after
 * every page but the last.
 */

/** Builds the chronicle; `seal` is what `Book.image` returned for the wax seal, or null when it could not be shipped. */
export function chroniclePages(seal: string | null): string[] {
    const sealImage = seal !== null ? `<img src='${seal}' width='220' height='220' align='center'/>` : "";

    return [
        "<title>The Chronicle of the Kingdoms Connected</title>" +
            "<subtitle>Being a True Account of how the Realm of Bohemia was made One for Many</subtitle>" +
            "<br/><br/><note>Set down by a humble brother of the scriptorium, in the year of Our Lord two thousand and twenty-six</note>",

        "<heading>Of the Lonely Realm</heading>" +
            "<paragraph><inc>I</inc>n the days before this chronicle, the realm of Bohemia was a lonely place. A man might ride from Trosky to the silver town of Kuttenberg and meet no soul but those the Maker had placed there, for every traveller walked his own road, and no road crossed another.</paragraph>" +
            "<paragraph>And it was said in the taverns that such was the nature of the land, and could not be otherwise.</paragraph>",

        "<heading>Of the Guild that Took Up the Work</heading>" +
            "<paragraph><inc>B</inc>ut there was a fellowship of craftsmen, the guild called <accent>MafiaHub</accent>, skilled in the joining of distant lands, who had laboured long in other kingdoms of the same art. They looked upon lonely Bohemia and said: let the roads be crossed, and let every traveller see his neighbour.</paragraph>" +
            "<paragraph>They asked no coin for the labour, neither did they serve any lord. Theirs was a free work, made by the common folk and for them, and they swore it upon their charter:</paragraph>" +
            "<poem>Free to join.<br/>Community made.<br/>Yours to shape.</poem>",

        "<heading>Of the First Bell</heading>" +
            "<paragraph><inc>O</inc>n the seventh day of September the first bell was rung, and the first true build of the Kingdoms Connected was given its name. Few heard it, for it rang only in the workshop, where the craftsmen tried their joints and mended what broke.</paragraph>" +
            "<paragraph>And on the sixteenth day of the same month the gates were opened to all comers, and this was called the <accent>Open Beta</accent>. On that day ten souls might stand together in one Bohemia. They climbed the same walls, vaulted the same fences, and laid their goods upon the same ground, and each saw the other do it.</paragraph>",

        "<heading>Of the Days of Swift Building</heading>" +
            "<paragraph><inc>T</inc>hereafter the craftsmen laboured without rest, and a new build was sent forth every day or two, as a mason lays a course of stone each morning.</paragraph>" +
            "<paragraph>Upon the seventeenth day they set up the great register of servers, that a traveller might find a hall to enter from within the game itself. Upon the eighteenth came the faithful hounds, and markers upon the map, and fire and smoke at the builder's command. Upon the twentieth came gestures and the choosing of faces, and quests shared among companions, and the art of building with one's own hands.</paragraph>" +
            "<paragraph>Upon the twenty-second the very wind was bound to blow alike for all, and the lords of each hall were given command of the weather, so that every soul beneath it shared <accent>the same clock and the same sky</accent>.</paragraph>",

        "<heading>Of the Great Muster</heading>" +
            "<paragraph><inc>A</inc>nd upon the twenty-sixth day of September there was a great muster, for the halls were made wide enough to hold <accent>five hundred and twelve</accent> souls in one Bohemia. On the same day the sword was given leave against the sword, and every blow, every block and every perfect block was judged but once, that no man could say he was struck unfairly.</paragraph>" +
            "<paragraph>Then came merchants at their stalls, and folk of the hall's own making who walked and spoke, and horses that could be wounded and healed. And on that day also the scribes taught the halls the letters of the Czech tongue, as was only fitting in this land.</paragraph>",

        "<heading>Of Thieves, Carts and Purses</heading>" +
            "<paragraph><inc>N</inc>or did the work slacken. There followed the cutting of purses between travellers, and tack for horses, and the carrying of the wounded upon one's shoulders, and benches to sit upon, and chests with locks and words of passage.</paragraph>" +
            "<paragraph>Upon the twenty-ninth day the halls themselves took keeping of every purse and pack, and the alchemist's craft was shared, and learning and the perks of skill, and horses a man might call his own. And at the month's end came carts drawn by horses, doors that every soul saw open and shut alike, trade at the vendor's table, and the eye that may look upon oneself from behind.</paragraph>",

        "<heading>Of the Scribes' Art</heading>" +
            "<paragraph><inc>W</inc>hat set this work apart from all others was this: that the lord of every hall might write its laws himself. In the tongues called JavaScript and TypeScript he may summon folk who walk and follow, raise stalls and set their prices, write quests into the journal, and paint whole windows in the manner of the web.</paragraph>" +
            "<paragraph>Even this very book was so made. No scribe of Warhorse ever bound it: it was written by a script, sealed with wax from no candle, and laid in thy hands by the hall thou standest in.</paragraph>" +
            sealImage,

        "<heading>Of What is Yet to Come</heading>" +
            "<paragraph><inc>H</inc>ere the chronicle must end, for the work is not finished, and every day adds a new page to it. Let it be known to all who read: this is a work of fans, freely given, and owes no fealty to Warhorse, to Deep Silver, nor to PLAION.</paragraph>" +
            "<paragraph>Go now, traveller. Seek thy companions in the great register of halls, or raise a hall of thine own and write its laws. Bohemia is lonely no longer.</paragraph>" +
            "<note>Explicit liber. Thanks be to God.</note>",
    ];
}

/** A short herald's letter, to show the `letter` carrier. */
export function letterPages(): string[] {
    return [
        "<paragraph>To every traveller who reads this, greetings.</paragraph>" +
            "<paragraph>Know that the halls of the Kingdoms Connected stand open, and that five hundred and twelve may now ride together where once each rode alone. Bring thy horse, thy hound and thy sword, and keep thy purse close, for there are cutpurses abroad.</paragraph>" +
            "<paragraph>Given under our seal, at the month's end of September, in the year of Our Lord two thousand and twenty-six.</paragraph>" +
            "<note>The Heralds of the Kingdoms Connected</note>",
    ];
}
