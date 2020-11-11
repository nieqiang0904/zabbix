<?php
/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

require_once dirname(__FILE__).'/../include/CWebTest.php';
require_once dirname(__FILE__).'/common/testFormTags.php';

/**
 * @backup triggers
 */
class testFormTagsTrigger extends testFormTags {

	public $update_name = 'Trigger with tags for updating';
	public $clone_name = 'Trigger with tags for cloning';
	public $link = 'triggers.php?filter_set=1&filter_hostids%5B0%5D=40001';
	public $saved_link = 'triggers.php?form=update&triggerid=';
	public $new_name = 'Cloned Trigger';

	/**
	 * Test creating of Trigger with tags.
	 *
	 * @dataProvider getCreateData
	 */
	public function testFormTagsTrigger_Create($data) {
		$expression = '{Simple form test host:test-item-reuse.last()}=0';
		$this->checkTagsCreate($data, 'trigger', $expression);
	}

	/**
	 * Test update of Trigger with tags.
	 *
	 * @dataProvider getUpdateData
	 */
	public function testFormTagsTrigger_Update($data) {
		$this->checkTagsUpdate($data, 'trigger');
	}

	/**
	 * Test cloning of Trigger with tags.
	 */
	public function testFormTagsTrigger_Clone() {
		$this->executeCloning('trigger', 'Clone');
	}
}
